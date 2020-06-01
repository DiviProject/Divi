#ifndef SUPERBLOCK_HELPERS_H
#define SUPERBLOCK_HELPERS_H
#include <stdint.h>
#include <amount.h>

class CBlockRewards;
class CChainParams;

namespace Legacy
{
    bool IsValidLotteryBlockHeight(int nBlockHeight,const CChainParams& chainParams);
    bool IsValidTreasuryBlockHeight(int nBlockHeight,const CChainParams& chainParams);
    int64_t GetTreasuryReward(const CBlockRewards &rewards);
    int64_t GetCharityReward(const CBlockRewards &rewards);
    int64_t GetLotteryReward(const CBlockRewards &rewards);
    CBlockRewards GetBlockSubsidity(int nHeight);
    CAmount GetFullBlockValue(int nHeight);
};

bool IsValidLotteryBlockHeight(int nBlockHeight);
bool IsValidTreasuryBlockHeight(int nBlockHeight);

int64_t GetTreasuryReward(const CBlockRewards &rewards);
int64_t GetCharityReward(const CBlockRewards &rewards);
int64_t GetLotteryReward(const CBlockRewards &rewards);

CBlockRewards GetBlockSubsidity(int nHeight);
CAmount GetFullBlockValue(int nHeight);

class SuperblockSubsidyHeightValidator
{
private:
    const CChainParams& chainParameters_;
    int transitionHeight_;
    int superblockCycleLength_;

public:
    SuperblockSubsidyHeightValidator(const CChainParams& chainParameters);
    int getTransitionHeight() const;
    const CChainParams& getChainParameters() const;

    bool IsValidLotteryBlockHeight(int nBlockHeight);
    bool IsValidTreasuryBlockHeight(int nBlockHeight);
};
#endif // SUPERBLOCK_HELPERS_H
