#ifndef SUPERBLOCK_HELPERS_H
#define SUPERBLOCK_HELPERS_H
class CChainParams;

namespace Legacy
{
    bool IsValidLotteryBlockHeight(int nBlockHeight,const CChainParams& chainParams);
    bool IsValidTreasuryBlockHeight(int nBlockHeight,const CChainParams& chainParams);
};

bool IsValidLotteryBlockHeight(int nBlockHeight);
bool IsValidTreasuryBlockHeight(int nBlockHeight);

class LotteryAndTreasuryBlockSubsidyIncentives
{
private:
    const CChainParams& chainParameters_;
    int transitionHeight_;
    int superblockCycleLength_;
public:
    LotteryAndTreasuryBlockSubsidyIncentives(const CChainParams& chainParameters);
    int getTransitionHeight() const;
    const CChainParams& getChainParameters() const;
    bool IsValidLotteryBlockHeight(int nBlockHeight);
    bool IsValidTreasuryBlockHeight(int nBlockHeight);
};
#endif // SUPERBLOCK_HELPERS_H
