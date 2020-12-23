#include <BlockSubsidyProvider.h>

#include <BlockRewards.h>
#include <chainparams.h>
#include <I_SuperblockHeightValidator.h>
#include <LegacyBlockSubsidies.h>

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
    return GetBlockSubsidity(nHeight).total();
}