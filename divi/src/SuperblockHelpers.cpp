#include <SuperblockHelpers.h>

#include <chainparams.h>
#include <BlockRewards.h>
#include <LegacyBlockSubsidies.h>
#include <SuperblockHeightValidator.h>
#include <BlockSubsidyProvider.h>

SuperblockSubsidyProvider::SuperblockSubsidyProvider(
    const CChainParams& chainParameters, 
    I_SuperblockHeightValidator& heightValidator,
    I_BlockSubsidyProvider& blockSubsidyProvider
    ): chainParameters_(chainParameters)
    , heightValidator_(heightValidator)
    , blockSubsidyProvider_(blockSubsidyProvider)
{
}

CAmount SuperblockSubsidyProvider::GetTreasuryReward(int blockHeight) const
{
    CAmount totalReward = 0;
    if(blockHeight==0) return totalReward;
    if(heightValidator_.IsValidTreasuryBlockHeight(blockHeight))
    {
        return blockSubsidyProvider_.GetBlockSubsidity(blockHeight).nTreasuryReward;
    }
    return totalReward;
}

CAmount SuperblockSubsidyProvider::GetCharityReward(int blockHeight) const
{
    CAmount totalReward = 0;
    if(blockHeight==0) return totalReward;
    if(heightValidator_.IsValidTreasuryBlockHeight(blockHeight))
    {
        return blockSubsidyProvider_.GetBlockSubsidity(blockHeight).nCharityReward;
    }
    return totalReward;
}

CAmount SuperblockSubsidyProvider::GetLotteryReward(int blockHeight) const
{
    CAmount totalReward = 0;
    if(blockHeight==0) return totalReward;
    if(heightValidator_.IsValidLotteryBlockHeight(blockHeight))
    {
        return blockSubsidyProvider_.GetBlockSubsidity(blockHeight).nLotteryReward;
    }
    return totalReward;
}

SuperblockSubsidyContainer::SuperblockSubsidyContainer(
    const CChainParams& chainParameters
    ): chainParameters_(chainParameters)
    , heightValidator_(std::make_shared<SuperblockHeightValidator>(chainParameters_))
    , blockSubsidies_(std::make_shared<BlockSubsidyProvider>(chainParameters_,*heightValidator_))
    , superblockSubsidies_(chainParameters_,*heightValidator_,*blockSubsidies_)
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
const SuperblockSubsidyProvider& SuperblockSubsidyContainer::superblockSubsidiesProvider() const
{
    return superblockSubsidies_;
}

// Non-Legacy methods

bool IsValidLotteryBlockHeight(int nBlockHeight)
{
    SuperblockSubsidyContainer subsidiesContainer(Params());
    return subsidiesContainer.superblockHeightValidator().IsValidLotteryBlockHeight(nBlockHeight);
}

bool IsValidTreasuryBlockHeight(int nBlockHeight)
{
    SuperblockSubsidyContainer subsidiesContainer(Params());
    return subsidiesContainer.superblockHeightValidator().IsValidTreasuryBlockHeight(nBlockHeight);
}

CBlockRewards GetBlockSubsidity(int nHeight)
{
    SuperblockSubsidyContainer subsidiesContainer(Params());
    return subsidiesContainer.blockSubsidiesProvider().GetBlockSubsidity(nHeight);
}