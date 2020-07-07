#include <SuperblockHelpers.h>

#include <chainparams.h>
#include <BlockRewards.h>
#include <LegacyBlockSubsidies.h>
#include <SuperblockHeightValidator.h>
#include <BlockSubsidyProvider.h>
#include <chain.h>
#include <blockmap.h>

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

bool IsBlockValueValid(const CBlock& block, const CBlockRewards &nExpectedValue, CAmount nMinted, int nHeight)
{
    auto nExpectedMintCombined = nExpectedValue.nStakeReward + nExpectedValue.nMasternodeReward;

    // here we expect treasury block payment
    if(IsValidTreasuryBlockHeight(nHeight)) {
        nExpectedMintCombined += (nExpectedValue.nTreasuryReward + nExpectedValue.nCharityReward);
    }
    else if(IsValidLotteryBlockHeight(nHeight)) {
        nExpectedMintCombined += nExpectedValue.nLotteryReward;
    }

    if (nMinted > nExpectedMintCombined) {
        return false;
    }

    return true;
}