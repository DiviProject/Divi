#ifndef BLOCK_SUBSIDY_PROVIDER_H
#define BLOCK_SUBSIDY_PROVIDER_H
#include <I_BlockSubsidyProvider.h>
class CChainParams;
class I_SuperblockHeightValidator;
class CBlockRewards;

class BlockSubsidyProvider: public I_BlockSubsidyProvider
{
private:
    const CChainParams& chainParameters_;
    I_SuperblockHeightValidator& heightValidator_;

    void updateTreasuryReward(int nHeight, CBlockRewards& rewards, bool isTreasuryBlock) const;
    void updateLotteryReward(int nHeight, CBlockRewards& rewards,bool isLotteryBlock) const;
public:
    BlockSubsidyProvider(
        const CChainParams& chainParameters,
        I_SuperblockHeightValidator& heightValidator);
    virtual CBlockRewards GetBlockSubsidity(int nHeight) const;
    virtual CAmount GetFullBlockValue(int nHeight) const;
};
#endif// BLOCK_SUBSIDY_PROVIDER_H