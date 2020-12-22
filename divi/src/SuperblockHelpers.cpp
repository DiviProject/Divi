#include <SuperblockHelpers.h>

#include <chainparams.h>
#include <BlockRewards.h>
#include <LegacyBlockSubsidies.h>
#include <SuperblockHeightValidator.h>
#include <BlockSubsidyProvider.h>

SuperblockSubsidyContainer::SuperblockSubsidyContainer(
    const CChainParams& chainParameters
    ): chainParameters_(chainParameters)
    , heightValidator_(std::make_shared<SuperblockHeightValidator>(chainParameters_))
    , blockSubsidies_(std::make_shared<BlockSubsidyProvider>(chainParameters_,*heightValidator_))
{
}

const I_SuperblockHeightValidator& SuperblockSubsidyContainer::superblockHeightValidator() const
{
    return *heightValidator_;
}
const I_BlockSubsidyProvider& SuperblockSubsidyContainer::blockSubsidiesProvider() const
{
    return *blockSubsidies_;
}