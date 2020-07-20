#ifndef SUPERBLOCK_HELPERS_H
#define SUPERBLOCK_HELPERS_H
#include <stdint.h>
#include <amount.h>
#include <I_SuperblockHeightValidator.h>
#include <I_BlockSubsidyProvider.h>
#include <memory>

class CBlockRewards;
class CChainParams;

bool IsBlockValueValid(const CBlockRewards &nExpectedValue, CAmount nMinted, int nHeight);

class I_SuperblockSubsidyContainer
{
public:
    virtual ~I_SuperblockSubsidyContainer(){};
    virtual const I_SuperblockHeightValidator& superblockHeightValidator() const = 0;
    virtual const I_BlockSubsidyProvider& blockSubsidiesProvider() const = 0;
};
class SuperblockSubsidyContainer: public I_SuperblockSubsidyContainer
{
private:
    const CChainParams& chainParameters_;
    std::shared_ptr<I_SuperblockHeightValidator> heightValidator_;
    std::shared_ptr<I_BlockSubsidyProvider> blockSubsidies_;
    
public:
    SuperblockSubsidyContainer(const CChainParams& chainParameters);
    virtual const I_SuperblockHeightValidator& superblockHeightValidator() const;
    virtual const I_BlockSubsidyProvider& blockSubsidiesProvider() const;
};
#endif // SUPERBLOCK_HELPERS_H
