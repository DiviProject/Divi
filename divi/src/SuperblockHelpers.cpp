#include <SuperblockHelpers.h>

#include <chainparams.h>
#include <BlockRewards.h>
#include <LegacyBlockSubsidies.h>
#include <SuperblockHeightValidator.h>


BlockSubsidyProvider::BlockSubsidyProvider(
    const CChainParams& chainParameters,
    I_SuperblockHeightValidator& heightValidator
    ): chainParameters_(chainParameters)
    , heightValidator_(heightValidator)
{

}

void BlockSubsidyProvider::updateTreasuryReward(int nHeight, CBlockRewards& rewards,bool isTreasuryBlock) const
{
    CAmount& treasuryReward = *const_cast<CAmount*>(&rewards.nTreasuryReward);
    CAmount& charityReward = *const_cast<CAmount*>(&rewards.nCharityReward);
    if(!isTreasuryBlock) 
    {
        treasuryReward = 0;
        charityReward =0;
    }
    int treasuryBlockCycleLength = heightValidator_.GetTreasuryBlockPaymentCycle(nHeight);
    int priorTreasuryBlockHeight = nHeight - treasuryBlockCycleLength;
    CBlockRewards priorRewards = isTreasuryBlock? Legacy::GetBlockSubsidity(priorTreasuryBlockHeight,chainParameters_): rewards;
    int numberOfSubsidyIntervals = nHeight/chainParameters_.SubsidyHalvingInterval(); // must be at least 2;
    int priorRewardWeight = numberOfSubsidyIntervals*chainParameters_.SubsidyHalvingInterval() - priorTreasuryBlockHeight;
    int currentRewardWeight =  nHeight - numberOfSubsidyIntervals*chainParameters_.SubsidyHalvingInterval();

    treasuryReward = priorRewards.nTreasuryReward * priorRewardWeight + rewards.nTreasuryReward* currentRewardWeight;
    charityReward = priorRewards.nCharityReward * priorRewardWeight + rewards.nCharityReward* currentRewardWeight;
}


void BlockSubsidyProvider::updateLotteryReward(int nHeight, CBlockRewards& rewards,bool isLotteryBlock) const
{
    CAmount& lotteryReward = *const_cast<CAmount*>(&rewards.nLotteryReward);
    if(!isLotteryBlock) 
    {
        lotteryReward = 0;
    }
    int lotteryBlockCycleLength = heightValidator_.GetLotteryBlockPaymentCycle(nHeight);
    int priorLotteryBlockHeight = nHeight - lotteryBlockCycleLength;
    CBlockRewards priorRewards = isLotteryBlock? Legacy::GetBlockSubsidity(priorLotteryBlockHeight,chainParameters_): rewards;
    int numberOfSubsidyIntervals = nHeight/chainParameters_.SubsidyHalvingInterval(); // must be at least 2;
    int priorRewardWeight = numberOfSubsidyIntervals*chainParameters_.SubsidyHalvingInterval() - priorLotteryBlockHeight;
    int currentRewardWeight =  nHeight - numberOfSubsidyIntervals*chainParameters_.SubsidyHalvingInterval();

    lotteryReward = priorRewards.nLotteryReward * priorRewardWeight + rewards.nLotteryReward* currentRewardWeight;
}


CBlockRewards BlockSubsidyProvider::GetBlockSubsidity(int nHeight) const
{
    CBlockRewards rewards = Legacy::GetBlockSubsidity(nHeight,chainParameters_);   
    updateTreasuryReward(nHeight,rewards, heightValidator_.IsValidTreasuryBlockHeight(nHeight));
    updateLotteryReward(nHeight,rewards, heightValidator_.IsValidLotteryBlockHeight(nHeight));
    return rewards;
}
CAmount BlockSubsidyProvider::GetFullBlockValue(int nHeight) const
{
    return Legacy::GetFullBlockValue(nHeight,chainParameters_);
}

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