#ifndef LOTTERY_WINNERS_CALCULATOR_H
#define LOTTERY_WINNERS_CALCULATOR_H
#include <LotteryCoinstakes.h>

class uint256;
class CTransaction;
class CChain;
class CSporkManager;
class I_SuperblockHeightValidator;
struct RankedScoreAwareCoinstakes;
class LotteryWinnersCalculator
{
private:
    const int startOfLotteryBlocks_;
    CChain& activeChain_;
    CSporkManager& sporkManager_;
    const I_SuperblockHeightValidator& superblockHeightValidator_;
    int minimumCoinstakeForTicket(int nHeight) const;
    bool IsPaymentScriptVetoed(const CScript& paymentScript, const int blockHeight) const;
    void SelectTopElevenBestCoinstakes(
        const RankedScoreAwareCoinstakes& rankedScoreAwareCoinstakes,
        LotteryCoinstakes& updatedCoinstakes,
        bool& shouldUpdateCoinstakeData) const;
public:
    LotteryWinnersCalculator(
        int startOfLotteryBlocks,
        CChain& activeChain,
        CSporkManager& sporkManager,
        const I_SuperblockHeightValidator& superblockHeightValidator);
    uint256 CalculateLotteryScore(const uint256 &hashCoinbaseTx, const uint256 &hashLastLotteryBlock) const;
    bool IsCoinstakeValidForLottery(const CTransaction &tx, int nHeight) const;
    uint256 GetLastLotteryBlockHashBeforeHeight(int blockHeight) const;
    bool UpdateCoinstakes(const uint256& lastLotteryBlockHash, LotteryCoinstakes& updatedCoinstakes) const;
    LotteryCoinstakeData CalculateUpdatedLotteryWinners(const CTransaction& coinMintTransaction, const LotteryCoinstakeData& previousBlockLotteryCoinstakeData, int nHeight) const;
};
#endif // LOTTERY_WINNERS_CALCULATOR_H