#ifndef LOTTERY_WINNERS_CALCULATOR_H
#define LOTTERY_WINNERS_CALCULATOR_H
#include <LotteryCoinstakes.h>

class uint256;
class CTransaction;
class CBlock;
class CBlockIndex;
class CChainParams;
class LotteryWinnersCalculator
{
private:
    const CChainParams& chainParameters_;
public:
    LotteryWinnersCalculator(const CChainParams& chainParameters);
    CScript GetScriptForLotteryPayment(const uint256 &hashWinningCoinstake) const;
    uint256 CalculateLotteryScore(const uint256 &hashCoinbaseTx, const uint256 &hashLastLotteryBlock) const;
    bool IsCoinstakeValidForLottery(const CTransaction &tx, int nHeight) const;
    LotteryCoinstakes CalculateLotteryWinners(const CBlock &block, const CBlockIndex *prevBlockIndex, int nHeight) const;
};
#endif // LOTTERY_WINNERS_CALCULATOR_H