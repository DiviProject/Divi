#ifndef SUPERBLOCK_HELPERS_H
#define SUPERBLOCK_HELPERS_H
#include <stdint.h>
#include <amount.h>
#include <I_SuperblockHeightValidator.h>
#include <I_BlockSubsidyProvider.h>
#include <memory>

class CBlockRewards;
class CChainParams;

bool IsValidLotteryBlockHeight(int nBlockHeight);
bool IsValidTreasuryBlockHeight(int nBlockHeight);

CBlockRewards GetBlockSubsidity(int nHeight);


class SuperblockSubsidyProvider
{
private:
    const CChainParams& chainParameters_;
    I_SuperblockHeightValidator& heightValidator_;
    I_BlockSubsidyProvider& blockSubsidyProvider_;
public:
    SuperblockSubsidyProvider(
        const CChainParams& chainParameters, 
        I_SuperblockHeightValidator& heightValidator,
        I_BlockSubsidyProvider& blockSubsidyProvider);
    
    CAmount GetTreasuryReward(int blockHeight) const;
    CAmount GetCharityReward(int blockHeight) const;
    CAmount GetLotteryReward(int blockHeight) const;
};

class SuperblockSubsidyContainer
{
private:
    const CChainParams& chainParameters_;
    std::shared_ptr<I_SuperblockHeightValidator> heightValidator_;
    std::shared_ptr<I_BlockSubsidyProvider> blockSubsidies_;
    SuperblockSubsidyProvider superblockSubsidies_;
public:
    SuperblockSubsidyContainer(const CChainParams& chainParameters);
    const I_SuperblockHeightValidator& superblockHeightValidator() const;
    const I_BlockSubsidyProvider& blockSubsidiesProvider() const;
    const SuperblockSubsidyProvider& superblockSubsidiesProvider() const;
};
#endif // SUPERBLOCK_HELPERS_H
