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

extern CChain chainActive;
extern BlockMap mapBlockIndex;
bool IsBlockValueValid(const CBlock& block, const CBlockRewards &nExpectedValue, CAmount nMinted, int nHeight)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return true;

    nHeight = 0;
    if (pindexPrev->GetBlockHash() == block.hashPrevBlock) {
        nHeight = pindexPrev->nHeight + 1;
    } else { //out of order
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
            nHeight = (*mi).second->nHeight + 1;
    }

    if (nHeight == 0) {
        LogPrint("masternode","IsBlockValueValid() : WARNING: Couldn't find previous block\n");
    }

    //LogPrintf("XX69----------> IsBlockValueValid(): nMinted: %d, nExpectedValue: %d\n", FormatMoney(nMinted), FormatMoney(nExpectedValue));

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