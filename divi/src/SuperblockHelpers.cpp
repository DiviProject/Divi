#include <SuperblockHelpers.h>

#include <SuperblockHeightValidator.h>
#include <BlockSubsidyProvider.h>

SuperblockSubsidyContainer::SuperblockSubsidyContainer(
    const CChainParams& chainParameters
    ): chainParameters_(chainParameters)
    , heightValidator_(new SuperblockHeightValidator(chainParameters_))
    , blockSubsidies_(new BlockSubsidyProvider(chainParameters_,*heightValidator_))
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