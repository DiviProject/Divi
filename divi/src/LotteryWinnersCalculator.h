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
class I_SuperblockHeightValidator;
class LotteryWinnersCalculator
{
private:
    const CChainParams& chainParameters_;
    CChain& activeChain_;
    CSporkManager& sporkManager_;
    const I_SuperblockHeightValidator& superblockHeightValidator_;
    int minimumCoinstakeForTicket(int nHeight) const;
public:
    LotteryWinnersCalculator(
        const CChainParams& chainParameters,
        CChain& activeChain,
        CSporkManager& sporkManager,
        const I_SuperblockHeightValidator& superblockHeightValidator);
    uint256 CalculateLotteryScore(const uint256 &hashCoinbaseTx, const uint256 &hashLastLotteryBlock) const;
    bool IsCoinstakeValidForLottery(const CTransaction &tx, int nHeight) const;
    LotteryCoinstakeData CalculateLotteryWinners(const CBlock &block, const CBlockIndex *prevBlockIndex, int nHeight) const;
};
#endif // LOTTERY_WINNERS_CALCULATOR_H