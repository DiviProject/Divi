#ifndef SUPERBLOCK_HELPERS_H
#define SUPERBLOCK_HELPERS_H
#include <stdint.h>
#include <I_SuperblockHeightValidator.h>
#include <I_BlockSubsidyProvider.h>
#include <I_SuperblockSubsidyContainer.h>
#include <memory>

class CChainParams;
class CSporkManager;

class SuperblockSubsidyContainer: public I_SuperblockSubsidyContainer
{
private:
    std::unique_ptr<I_SuperblockHeightValidator> heightValidator_;
    std::unique_ptr<I_BlockSubsidyProvider> blockSubsidies_;

public:
    SuperblockSubsidyContainer(const CChainParams& chainParameters, const CSporkManager& sporkManager);
    ~SuperblockSubsidyContainer();
    virtual const I_SuperblockHeightValidator& superblockHeightValidator() const;
    virtual const I_BlockSubsidyProvider& blockSubsidiesProvider() const;
};
#endif // SUPERBLOCK_HELPERS_H
