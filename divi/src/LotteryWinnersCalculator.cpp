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

bool LotteryWinnersCalculator::UpdateCoinstakes(const uint256& lastLotteryBlockHash, LotteryCoinstakes& updatedCoinstakes) const
{
    using LotteryScore = uint256;
    std::vector<LotteryScore> scores;
    scores.reserve(updatedCoinstakes.size());
    for(auto&& lotteryCoinstake : updatedCoinstakes) {
        scores.emplace_back(CalculateLotteryScore(lotteryCoinstake.first, lastLotteryBlockHash));
    }

    // biggest entry at the begining
    bool shouldUpdateCoinstakeData = true;
    if(scores.size() > 1)
    {
        std::vector<unsigned> initialRanks(scores.size());
        std::iota(initialRanks.begin(),initialRanks.end(),0);

        std::stable_sort(std::begin(initialRanks), std::end(initialRanks),
            [&scores](const unsigned& lhs, const unsigned& rhs)
            {
                return scores[lhs] > scores[rhs];
            }
        );
        shouldUpdateCoinstakeData = initialRanks.back() != 11;

        if(shouldUpdateCoinstakeData)
        {
            auto it = std::find(initialRanks.begin(), initialRanks.end(), scores.size()-1);
            updatedCoinstakes.insert(
                updatedCoinstakes.begin() + std::distance(initialRanks.begin(),it), updatedCoinstakes.back() );
            updatedCoinstakes.pop_back();
        }
    }
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

    auto hashLastLotteryBlock = GetLastLotteryBlockHashBeforeHeight(nHeight);
    LotteryCoinstakes updatedCoinstakes = previousBlockLotteryCoinstakeData.getLotteryCoinstakes();
    updatedCoinstakes.emplace_back(coinMintTransaction.GetHash(), coinMintTransaction.IsCoinBase()? coinMintTransaction.vout[0].scriptPubKey:coinMintTransaction.vout[1].scriptPubKey);


    if(UpdateCoinstakes(hashLastLotteryBlock,updatedCoinstakes))
    {
        updatedCoinstakes.resize( std::min( std::size_t(11), updatedCoinstakes.size()) );
        return LotteryCoinstakeData(nHeight,updatedCoinstakes);
    }
    else
    {
        return previousBlockLotteryCoinstakeData.getShallowCopy();
    }
}