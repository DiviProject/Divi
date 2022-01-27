#ifndef LOTTERY_WINNERS_CALCULATOR_H
#define LOTTERY_WINNERS_CALCULATOR_H
#include <LotteryCoinstakes.h>
#include <amount.h>

class CBlockIndex;
class CTransaction;
class CChain;
class CSporkManager;
class I_SuperblockHeightValidator;

struct RankAwareScore
{
    uint256 score;
    size_t rank;
    bool isDuplicateScript;
};
using RankedScoreAwareCoinstakes = std::map<uint256,RankAwareScore>;
class LotteryWinnersCalculator
{
private:
    const int startOfLotteryBlocks_;
    const CChain& activeChain_;
    const CSporkManager& sporkManager_;
    const I_SuperblockHeightValidator& superblockHeightValidator_;
    CAmount minimumCoinstakeForTicket(int nHeight) const;
    bool IsPaymentScriptVetoed(const CScript& paymentScript, const int blockHeight) const;
    bool TopElevenBestCoinstakesNeedUpdating(
        bool trimDuplicates,
        const RankedScoreAwareCoinstakes& rankedScoreAwareCoinstakes,
        LotteryCoinstakes& updatedCoinstakes) const;
public:
    LotteryWinnersCalculator(
        int startOfLotteryBlocks,
        const CChain& activeChain,
        const CSporkManager& sporkManager,
        const I_SuperblockHeightValidator& superblockHeightValidator);
    static uint256 CalculateLotteryScore(const uint256 &hashCoinbaseTx, const uint256 &hashLastLotteryBlock);
    static RankedScoreAwareCoinstakes computeRankedScoreAwareCoinstakes(
        const uint256& lastLotteryBlockHash, const LotteryCoinstakes& updatedCoinstakes);
    bool IsCoinstakeValidForLottery(const CTransaction &tx, int nHeight) const;
    const CBlockIndex* GetLastLotteryBlockIndexBeforeHeight(int blockHeight) const;
    bool UpdateCoinstakes(int nextBlockHeight, LotteryCoinstakes& updatedCoinstakes) const;
    LotteryCoinstakeData CalculateUpdatedLotteryWinners(const CTransaction& coinMintTransaction, const LotteryCoinstakeData& previousBlockLotteryCoinstakeData, int nHeight) const;
};
#endif // LOTTERY_WINNERS_CALCULATOR_H
