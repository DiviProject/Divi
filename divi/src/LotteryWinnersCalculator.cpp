#include <LotteryWinnersCalculator.h>

#include <SuperblockHelpers.h>
#include <hash.h>
#include <uint256.h>
#include <primitives/transaction.h>
#include <primitives/block.h>
#include <chain.h>
#include <chainparams.h>
#include <timedata.h>
#include <numeric>
#include <spork.h>
#include <BlockDiskAccessor.h>
#include <I_SuperblockHeightValidator.h>

LotteryWinnersCalculator::LotteryWinnersCalculator(
    const CChainParams& chainParameters,
    CChain& activeChain,
    CSporkManager& sporkManager,
    const I_SuperblockHeightValidator& superblockHeightValidator
    ): chainParameters_(chainParameters)
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

LotteryCoinstakes LotteryWinnersCalculator::CalculateLotteryWinners(const CBlock &block, const CBlockIndex *prevBlockIndex, int nHeight) const
{
    LotteryCoinstakes result;
    // if that's a block when lottery happens, reset score for whole cycle
    if(superblockHeightValidator_.IsValidLotteryBlockHeight(nHeight))
        return result;

    if(!prevBlockIndex)
        return result;

    const int lotteryBlockPaymentCycle = superblockHeightValidator_.GetLotteryBlockPaymentCycle(nHeight);
    int nLastLotteryHeight = std::max(chainParameters_.GetLotteryBlockStartBlock(),  lotteryBlockPaymentCycle* ((nHeight - 1) / lotteryBlockPaymentCycle) );

    if(nHeight <= nLastLotteryHeight) {
        return result;
    }

    const auto& coinbaseTx = (nHeight > chainParameters_.LAST_POW_BLOCK() ? block.vtx[1] : block.vtx[0]);

    if(!IsCoinstakeValidForLottery(coinbaseTx, nHeight)) {
        return prevBlockIndex->vLotteryWinnersCoinstakes; // return last if we have no lotter participant in this block
    }

    CBlockIndex* prevLotteryBlockIndex = activeChain_[nLastLotteryHeight];
    auto hashLastLotteryBlock = prevLotteryBlockIndex->GetBlockHash();
    // lotteryWinnersCoinstakes has hashes of coinstakes, let calculate old scores + new score
    using LotteryScore = uint256;
    std::vector<LotteryScore> scores;
    scores.reserve(prevBlockIndex->vLotteryWinnersCoinstakes.size()+1);

    int startingWinnerIndex = 0;
    std::map<uint256,int> transactionHashToWinnerIndex;
    for(auto&& lotteryCoinstake : prevBlockIndex->vLotteryWinnersCoinstakes) {
        transactionHashToWinnerIndex[lotteryCoinstake.first] = startingWinnerIndex++;
        scores.emplace_back(CalculateLotteryScore(lotteryCoinstake.first, hashLastLotteryBlock));
    }

    auto newScore = CalculateLotteryScore(coinbaseTx.GetHash(), hashLastLotteryBlock);
    scores.emplace_back(newScore);
    transactionHashToWinnerIndex[coinbaseTx.GetHash()] = startingWinnerIndex++;

    result = prevBlockIndex->vLotteryWinnersCoinstakes;
    result.reserve(prevBlockIndex->vLotteryWinnersCoinstakes.size()+1);
    result.push_back(std::make_pair(coinbaseTx.GetHash(), coinbaseTx.IsCoinBase()? coinbaseTx.vout[0].scriptPubKey:coinbaseTx.vout[1].scriptPubKey));


    // biggest entry at the begining
    if(scores.size() > 1)
    {
        std::stable_sort(std::begin(result), std::end(result), 
            [&scores,&transactionHashToWinnerIndex](const LotteryCoinstake& lhs, const LotteryCoinstake& rhs) 
            {
                return scores[transactionHashToWinnerIndex[lhs.first]] > scores[transactionHashToWinnerIndex[rhs.first]];
            }
        );
    }
    result.resize( std::min( std::size_t(11), result.size()) );

    return result;
}