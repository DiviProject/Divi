#ifndef LOTTERY_WINNERS_CALCULATOR_H
#define LOTTERY_WINNERS_CALCULATOR_H
#include <LotteryCoinstakes.h>

class uint256;
class CTransaction;
class CBlock;
class CBlockIndex;

namespace LotteryWinnersCalculator
{
    CScript GetScriptForLotteryPayment(const uint256 &hashWinningCoinstake);
    uint256 CalculateLotteryScore(const uint256 &hashCoinbaseTx, const uint256 &hashLastLotteryBlock);
    bool IsCoinstakeValidForLottery(const CTransaction &tx, int nHeight);
    LotteryCoinstakes CalculateLotteryWinners(const CBlock &block, const CBlockIndex *prevBlockIndex, int nHeight);
}
#endif // LOTTERY_WINNERS_CALCULATOR_H