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

class SuperblockSubsidyContainer
{
private:
    const CChainParams& chainParameters_;
    std::shared_ptr<I_SuperblockHeightValidator> heightValidator_;
    std::shared_ptr<I_BlockSubsidyProvider> blockSubsidies_;
    
public:
    SuperblockSubsidyContainer(const CChainParams& chainParameters);
    const I_SuperblockHeightValidator& superblockHeightValidator() const;
    const I_BlockSubsidyProvider& blockSubsidiesProvider() const;
};
#endif // SUPERBLOCK_HELPERS_H
