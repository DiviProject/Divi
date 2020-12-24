#include <SuperblockSubsidyContainer.h>

#include <SuperblockHeightValidator.h>
#include <BlockSubsidyProvider.h>

SuperblockSubsidyContainer::SuperblockSubsidyContainer(
    const CChainParams& chainParameters
    ): heightValidator_(new SuperblockHeightValidator(chainParameters))
    , blockSubsidies_(new BlockSubsidyProvider(chainParameters,*heightValidator_))
{
}

SuperblockSubsidyContainer::~SuperblockSubsidyContainer()
{
    blockSubsidies_.reset();
    heightValidator_.reset();
}

const I_SuperblockHeightValidator& SuperblockSubsidyContainer::superblockHeightValidator() const
{
    return *heightValidator_;
}
const I_BlockSubsidyProvider& SuperblockSubsidyContainer::blockSubsidiesProvider() const
{
    return *blockSubsidies_;
}