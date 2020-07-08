#ifndef LOTTERY_WINNERS_CALCULATOR_H
#define LOTTERY_WINNERS_CALCULATOR_H
#include <LotteryCoinstakes.h>

class uint256;
class CTransaction;
class CBlock;
class CBlockIndex;
class CChainParams;
class CChain;
class CSporkManager;
class LotteryWinnersCalculator
{
private:
    const CChainParams& chainParameters_;
    CChain& activeChain_;
    CSporkManager& sporkManager_;
    int minimumCoinstakeForTicket(int nHeight) const;
public:
    LotteryWinnersCalculator(
        const CChainParams& chainParameters,
        CChain& activeChain,
        CSporkManager& sporkManager);
    uint256 CalculateLotteryScore(const uint256 &hashCoinbaseTx, const uint256 &hashLastLotteryBlock) const;
    bool IsCoinstakeValidForLottery(const CTransaction &tx, int nHeight) const;
    LotteryCoinstakes CalculateLotteryWinners(const CBlock &block, const CBlockIndex *prevBlockIndex, int nHeight) const;
};
#endif // LOTTERY_WINNERS_CALCULATOR_H