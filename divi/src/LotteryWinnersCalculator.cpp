#include <LotteryWinnersCalculator.h>

#include <SuperblockHelpers.h>
#include <hash.h>
#include <uint256.h>
#include <primitives/transaction.h>
#include <chain.h>
#include <timedata.h>
#include <numeric>
#include <spork.h>
#include <BlockDiskAccessor.h>
#include <I_SuperblockHeightValidator.h>


LotteryWinnersCalculator::LotteryWinnersCalculator(
    int startOfLotteryBlocks,
    CChain& activeChain,
    CSporkManager& sporkManager,
    const I_SuperblockHeightValidator& superblockHeightValidator
    ): startOfLotteryBlocks_(startOfLotteryBlocks)
    , activeChain_(activeChain)
    , sporkManager_(sporkManager)
    , superblockHeightValidator_(superblockHeightValidator)
{
}

int LotteryWinnersCalculator::minimumCoinstakeForTicket(int nHeight) const
{
    int nMinStakeValue = 10000; // default is 10k

    if(sporkManager_.IsSporkActive(SPORK_16_LOTTERY_TICKET_MIN_VALUE)) {
        MultiValueSporkList<LotteryTicketMinValueSporkValue> vValues;
        CSporkManager::ConvertMultiValueSporkVector(sporkManager_.GetMultiValueSpork(SPORK_16_LOTTERY_TICKET_MIN_VALUE), vValues);
        auto nBlockTime = activeChain_[nHeight] ? activeChain_[nHeight]->nTime : GetAdjustedTime();
        LotteryTicketMinValueSporkValue activeSpork = CSporkManager::GetActiveMultiValueSpork(vValues, nHeight, nBlockTime);

        if(activeSpork.IsValid()) {
            // we expect that this value is in coins, not in satoshis
            nMinStakeValue = activeSpork.nEntryTicketValue;
        }
    }

    return nMinStakeValue;
}

uint256 LotteryWinnersCalculator::CalculateLotteryScore(const uint256 &hashCoinbaseTx, const uint256 &hashLastLotteryBlock) const
{
    // Deterministically calculate a "score" for a Masternode based on any given (block)hash
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hashCoinbaseTx << hashLastLotteryBlock;
    return ss.GetHash();
}

bool LotteryWinnersCalculator::IsCoinstakeValidForLottery(const CTransaction &tx, int nHeight) const
{
    CAmount nAmount = 0;
    if(tx.IsCoinBase()) {
        nAmount = tx.vout[0].nValue;
    }
    else {
        auto payee = tx.vout[1].scriptPubKey;
        nAmount = std::accumulate(std::begin(tx.vout), std::end(tx.vout), CAmount(0), [payee](CAmount accum, const CTxOut &out) {
                return out.scriptPubKey == payee ? accum + out.nValue : accum;
    });
    }

    return nAmount > minimumCoinstakeForTicket(nHeight) * COIN; // only if stake is more than 10k
}

uint256 LotteryWinnersCalculator::GetLastLotteryBlockHashBeforeHeight(int blockHeight) const
{
    const int lotteryBlockPaymentCycle = superblockHeightValidator_.GetLotteryBlockPaymentCycle(blockHeight);
    const int nLastLotteryHeight = std::max(startOfLotteryBlocks_,  lotteryBlockPaymentCycle* ((blockHeight - 1) / lotteryBlockPaymentCycle) );
    return activeChain_[nLastLotteryHeight]->GetBlockHash();
}

bool LotteryWinnersCalculator::IsPaymentScriptVetoed(const CScript& paymentScript, const int blockHeight) const
{
    const int lotteryBlockPaymentCycle = superblockHeightValidator_.GetLotteryBlockPaymentCycle(blockHeight);
    const int nLastLotteryHeight = std::max(startOfLotteryBlocks_,  lotteryBlockPaymentCycle* ((blockHeight - 1) / lotteryBlockPaymentCycle) );
    constexpr int numberOfLotteryCyclesToVetoFor = 3;
    for (int lotteryCycleCount = 0; lotteryCycleCount < numberOfLotteryCyclesToVetoFor; ++lotteryCycleCount)
    {
        CBlockIndex* blockIndexPreceedingPriorLotteryBlock = activeChain_[ nLastLotteryHeight-lotteryBlockPaymentCycle*lotteryCycleCount-1];
        if(!blockIndexPreceedingPriorLotteryBlock)
        {
            return false;
        }
        const LotteryCoinstakes& previousWinners = blockIndexPreceedingPriorLotteryBlock->vLotteryWinnersCoinstakes.getLotteryCoinstakes();
        LotteryCoinstakes::const_iterator it = std::find_if(previousWinners.begin(),previousWinners.end(),
            [&paymentScript](const LotteryCoinstake& coinstake){
                return coinstake.second == paymentScript;
            });
        if(it != previousWinners.end()) return true;
    }
    return false;
}

struct RankAwareScore
{
    uint256 score;
    size_t rank;
    bool isDuplicateScript;
};
struct RankedScoreAwareCoinstakes: public std::map<uint256,RankAwareScore>
{
};

static void SortCoinstakesByScore(const RankedScoreAwareCoinstakes& rankedScoreAwareCoinstakes, LotteryCoinstakes& updatedCoinstakes)
{
    if(rankedScoreAwareCoinstakes.size() > 1)
    {
        std::stable_sort(std::begin(updatedCoinstakes), std::end(updatedCoinstakes),
            [&rankedScoreAwareCoinstakes](const LotteryCoinstake& lhs, const LotteryCoinstake& rhs)
            {
                return rankedScoreAwareCoinstakes.find(lhs.first)->second.score > rankedScoreAwareCoinstakes.find(rhs.first)->second.score;
            }
        );
    }
}

void LotteryWinnersCalculator::SelectTopElevenBestCoinstakes(
    const RankedScoreAwareCoinstakes& rankedScoreAwareCoinstakes,
    LotteryCoinstakes& updatedCoinstakes,
    bool& shouldUpdateCoinstakeData) const
{
    if( updatedCoinstakes.size() > 11)
    {
        LotteryCoinstakes::reverse_iterator rIteratorToLastDuplicate =
            std::find_if(updatedCoinstakes.rbegin(),updatedCoinstakes.rend(),
            [&rankedScoreAwareCoinstakes](const LotteryCoinstake& coinstake)
            {
                return rankedScoreAwareCoinstakes.find(coinstake.first)->second.isDuplicateScript;
            });
        if(rIteratorToLastDuplicate != updatedCoinstakes.rend())
        {
            updatedCoinstakes.erase((rIteratorToLastDuplicate+1).base());
            shouldUpdateCoinstakeData = true;
        }
        else
        {
            updatedCoinstakes.pop_back();
        }
    }
}

bool RemoveDuplicateWinners(const LotteryCoinstake& newestCoinstake, LotteryCoinstakes& updatedCoinstakes)
{
    if(updatedCoinstakes.size()>1)
    {
        const CScript& duplicateToCheckFor = newestCoinstake.second;
        LotteryCoinstakes::iterator iteratorToFirstInstance = std::find_if(updatedCoinstakes.begin(),updatedCoinstakes.end(),
            [&duplicateToCheckFor](const LotteryCoinstake& coinstake)
            {
                return coinstake.second == duplicateToCheckFor;
            } );
        LotteryCoinstakes::iterator iteratorToSecondInstance = std::find_if(std::next(iteratorToFirstInstance),updatedCoinstakes.end(),
            [&duplicateToCheckFor](const LotteryCoinstake& coinstake)
            {
                return coinstake.second == duplicateToCheckFor;
            } );
        if(iteratorToSecondInstance != updatedCoinstakes.end())
        {
            bool newestCoinstakeIsKept = newestCoinstake.first != iteratorToSecondInstance->first;
            updatedCoinstakes.erase(iteratorToSecondInstance);
            return newestCoinstakeIsKept;
        }
    }
    return false;
}

bool LotteryWinnersCalculator::UpdateCoinstakes(const uint256& lastLotteryBlockHash, LotteryCoinstakes& updatedCoinstakes) const
{

    RankedScoreAwareCoinstakes rankedScoreAwareCoinstakes;
    std::set<CScript> paymentScripts;
    for(const auto& lotteryCoinstake : updatedCoinstakes)
    {
        RankAwareScore rankedScore = {
            CalculateLotteryScore(lotteryCoinstake.first,
            lastLotteryBlockHash), rankedScoreAwareCoinstakes.size(),
            paymentScripts.count(lotteryCoinstake.second)>0  };
        rankedScoreAwareCoinstakes.emplace(lotteryCoinstake.first, std::move(rankedScore));
        paymentScripts.insert(lotteryCoinstake.second);
    }
    const LotteryCoinstake newestCoinstake = updatedCoinstakes.back();

    // biggest entry at the begining
    bool shouldUpdateCoinstakeData = rankedScoreAwareCoinstakes.size() > 0;
    SortCoinstakesByScore(rankedScoreAwareCoinstakes,updatedCoinstakes);
    if(rankedScoreAwareCoinstakes.size() > 1)
    {
        shouldUpdateCoinstakeData = rankedScoreAwareCoinstakes[updatedCoinstakes.back().first].rank != 11;
    }
    SelectTopElevenBestCoinstakes(rankedScoreAwareCoinstakes,updatedCoinstakes,shouldUpdateCoinstakeData);
    return shouldUpdateCoinstakeData;
}

LotteryCoinstakeData LotteryWinnersCalculator::CalculateUpdatedLotteryWinners(
    const CTransaction& coinMintTransaction,
    const LotteryCoinstakeData& previousBlockLotteryCoinstakeData,
    int nHeight) const
{
    if(nHeight <= 0) return LotteryCoinstakeData();
    if(superblockHeightValidator_.IsValidLotteryBlockHeight(nHeight)) return LotteryCoinstakeData(nHeight);
    if(nHeight <= startOfLotteryBlocks_) return previousBlockLotteryCoinstakeData.getShallowCopy();
    if(!IsCoinstakeValidForLottery(coinMintTransaction, nHeight)) return previousBlockLotteryCoinstakeData.getShallowCopy();

    LotteryCoinstakes updatedCoinstakes = previousBlockLotteryCoinstakeData.getLotteryCoinstakes();
    updatedCoinstakes.emplace_back(coinMintTransaction.GetHash(), coinMintTransaction.IsCoinBase()? coinMintTransaction.vout[0].scriptPubKey:coinMintTransaction.vout[1].scriptPubKey);

    if(IsPaymentScriptVetoed(updatedCoinstakes.back().second,nHeight))
    {
        return previousBlockLotteryCoinstakeData.getShallowCopy();
    }

    if(UpdateCoinstakes(GetLastLotteryBlockHashBeforeHeight(nHeight),updatedCoinstakes))
    {
        return LotteryCoinstakeData(nHeight,updatedCoinstakes);
    }
    else
    {
        return previousBlockLotteryCoinstakeData.getShallowCopy();
    }
}