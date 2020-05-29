#ifndef SUPERBLOCK_HELPERS_H
#define SUPERBLOCK_HELPERS_H
bool OldIsValidLotteryBlockHeight(int nBlockHeight);
bool OldIsValidTreasuryBlockHeight(int nBlockHeight);

bool IsValidLotteryBlockHeight(int nBlockHeight);
bool IsValidTreasuryBlockHeight(int nBlockHeight);

class CChainParams;

class LotteryAndTreasuryBlockSubsidyIncentives
{
private:
    CChainParams& chainParameters_;
    bool OldIsValidLotteryBlockHeight(int nBlockHeight);
    bool OldIsValidTreasuryBlockHeight(int nBlockHeight);

public:
    LotteryAndTreasuryBlockSubsidyIncentives(CChainParams& chainParameters);
    bool IsValidLotteryBlockHeight(int nBlockHeight);
    bool IsValidTreasuryBlockHeight(int nBlockHeight);
};
#endif // SUPERBLOCK_HELPERS_H
