#include <SuperblockHelpers.h>

#include <chainparams.h>
#include <BlockRewards.h>
#include <spork.h>
#include <timedata.h>
#include <chain.h>

extern CChain chainActive;

// Legacy methods
CAmount Legacy::GetFullBlockValue(int nHeight, const CChainParams& chainParameters)
{

    if(nHeight == 0) {
        return 50 * COIN;
    } else if (nHeight == 1) {
        return chainParameters.premineAmt;
    }

    if(sporkManager.IsSporkActive(SPORK_15_BLOCK_VALUE)) {
        MultiValueSporkList<BlockSubsiditySporkValue> vBlockSubsiditySporkValues;
        CSporkManager::ConvertMultiValueSporkVector(sporkManager.GetMultiValueSpork(SPORK_15_BLOCK_VALUE), vBlockSubsiditySporkValues);
        auto nBlockTime = chainActive[nHeight] ? chainActive[nHeight]->nTime : GetAdjustedTime();
        BlockSubsiditySporkValue activeSpork = CSporkManager::GetActiveMultiValueSpork(vBlockSubsiditySporkValues, nHeight, nBlockTime);

        if(activeSpork.IsValid()) {
            // we expect that this value is in coins, not in satoshis
            return activeSpork.nBlockSubsidity * COIN;
        }
    }

    CAmount nSubsidy = 1250;
    auto nSubsidyHalvingInterval = chainParameters.SubsidyHalvingInterval();
    // first two intervals == two years, same amount 1250
    for (int i = nSubsidyHalvingInterval * 2; i <= nHeight; i += nSubsidyHalvingInterval) {
        nSubsidy -= 100;
    }

    return std::max<CAmount>(nSubsidy, 250) * COIN;
}

CBlockRewards Legacy::GetBlockSubsidity(int nHeight, const CChainParams& chainParameters)
{
    CAmount nSubsidy = Legacy::GetFullBlockValue(nHeight,chainParameters);

    if(nHeight <= chainParameters.LAST_POW_BLOCK()) {
        return CBlockRewards(nSubsidy, 0, 0, 0, 0, 0);
    }

    CAmount nLotteryPart = (nHeight >= chainParameters.GetLotteryBlockStartBlock()) ? (50 * COIN) : 0;

    nSubsidy -= nLotteryPart;

    auto helper = [nHeight,&chainParameters,nSubsidy](int nStakePercentage, int nMasternodePercentage, int nTreasuryPercentage, int nProposalsPercentage, int nCharityPercentage) {
        auto helper = [nSubsidy](int percentage) {
            return (nSubsidy * percentage) / 100;
        };

        return CBlockRewards(
            helper(nStakePercentage), 
            helper(nMasternodePercentage), 
            helper(nTreasuryPercentage), 
            helper(nCharityPercentage),
            50 * COIN, 
            helper(nProposalsPercentage));
    };

    if(sporkManager.IsSporkActive(SPORK_13_BLOCK_PAYMENTS)) {
        MultiValueSporkList<BlockPaymentSporkValue> vBlockPaymentsValues;
        CSporkManager::ConvertMultiValueSporkVector(sporkManager.GetMultiValueSpork(SPORK_13_BLOCK_PAYMENTS), vBlockPaymentsValues);
        auto nBlockTime = chainActive[nHeight] ? chainActive[nHeight]->nTime : GetAdjustedTime();
        BlockPaymentSporkValue activeSpork = CSporkManager::GetActiveMultiValueSpork(vBlockPaymentsValues, nHeight, nBlockTime);

        if(activeSpork.IsValid()) {
            // we expect that this value is in coins, not in satoshis
            return helper(activeSpork.nStakeReward, activeSpork.nMasternodeReward,
                          activeSpork.nTreasuryReward, activeSpork.nProposalsReward, activeSpork.nCharityReward);
        }
    }


    return helper(38, 45, 16, 0, 1);
}


bool Legacy::IsValidLotteryBlockHeight(int nBlockHeight,const CChainParams& chainParams)
{
    return nBlockHeight >= chainParams.GetLotteryBlockStartBlock() &&
            ((nBlockHeight % chainParams.GetLotteryBlockCycle()) == 0);
}

bool Legacy::IsValidTreasuryBlockHeight(int nBlockHeight,const CChainParams& chainParams)
{
    return nBlockHeight >= chainParams.GetTreasuryPaymentsStartBlock() &&
            ((nBlockHeight % chainParams.GetTreasuryPaymentsCycle()) == 0);
}

int64_t Legacy::GetTreasuryReward(const CBlockRewards &rewards, const CChainParams& chainParameters)
{
    return rewards.nTreasuryReward*chainParameters.GetTreasuryPaymentsCycle();
}

int64_t Legacy::GetCharityReward(const CBlockRewards &rewards, const CChainParams& chainParameters)
{
    return rewards.nCharityReward*chainParameters.GetTreasuryPaymentsCycle();
}

int64_t Legacy::GetLotteryReward(const CBlockRewards &rewards, const CChainParams& chainParameters)
{
    // 50 coins every block for lottery
    return rewards.nLotteryReward*chainParameters.GetLotteryBlockCycle();
}

// Non-Legacy methods

bool IsValidLotteryBlockHeight(int nBlockHeight)
{
    SuperblockHeightValidator incentives(Params());
    return incentives.IsValidLotteryBlockHeight(nBlockHeight);
}

bool IsValidTreasuryBlockHeight(int nBlockHeight)
{
    SuperblockHeightValidator incentives(Params());
    return incentives.IsValidTreasuryBlockHeight(nBlockHeight);
}

int64_t GetTreasuryReward(const CBlockRewards &rewards)
{
    return Legacy::GetTreasuryReward(rewards,Params());
}
int64_t GetCharityReward(const CBlockRewards &rewards)
{
    return Legacy::GetCharityReward(rewards,Params());
}
int64_t GetLotteryReward(const CBlockRewards &rewards)
{
    return Legacy::GetLotteryReward(rewards,Params());
}
CBlockRewards GetBlockSubsidity(int nHeight)
{
    return Legacy::GetBlockSubsidity(nHeight,Params());
}


SuperblockHeightValidator::SuperblockHeightValidator(
    const CChainParams& chainParameters
    ): chainParameters_(chainParameters)
    , transitionHeight_(chainParameters_.GetLotteryBlockCycle()*chainParameters_.GetTreasuryPaymentsCycle())
    , superblockCycleLength_((chainParameters_.GetLotteryBlockCycle()+chainParameters_.GetTreasuryPaymentsCycle())/2)
{
}

bool SuperblockHeightValidator::IsValidLotteryBlockHeight(int nBlockHeight) const
{
    if(nBlockHeight < transitionHeight_)
    {
        return Legacy::IsValidLotteryBlockHeight(nBlockHeight,chainParameters_);
    }
    else
    {
        return ((nBlockHeight - transitionHeight_) % superblockCycleLength_) == 0;
    }
}
bool SuperblockHeightValidator::IsValidTreasuryBlockHeight(int nBlockHeight) const
{
    if(nBlockHeight < transitionHeight_)
    {
        return Legacy::IsValidTreasuryBlockHeight(nBlockHeight,chainParameters_);
    }
    else
    {
        return IsValidLotteryBlockHeight(nBlockHeight-1);
    }
}

int SuperblockHeightValidator::getTransitionHeight() const
{
    return transitionHeight_;
}

const CChainParams& SuperblockHeightValidator::getChainParameters() const
{
    return chainParameters_;
}

int SuperblockHeightValidator::GetTreasuryBlockPaymentCycle(int nBlockHeight) const
{
    return (nBlockHeight < transitionHeight_)? chainParameters_.GetTreasuryPaymentsCycle():
        ((nBlockHeight <= transitionHeight_+1)? chainParameters_.GetTreasuryPaymentsCycle()+1: superblockCycleLength_);
}
int SuperblockHeightValidator::GetLotteryBlockPaymentCycle(int nBlockHeight) const
{
    return (nBlockHeight < transitionHeight_)? chainParameters_.GetLotteryBlockCycle(): superblockCycleLength_;
}

BlockSubsidyProvider::BlockSubsidyProvider(
    const CChainParams& chainParameters,
    I_SuperblockHeightValidator& heightValidator
    ): chainParameters_(chainParameters)
    , heightValidator_(heightValidator)
{

}

CBlockRewards BlockSubsidyProvider::GetBlockSubsidity(int nHeight) const
{
    CBlockRewards rewards = Legacy::GetBlockSubsidity(nHeight,chainParameters_);
    *const_cast<CAmount*>(&rewards.nTreasuryReward) *= (heightValidator_.IsValidTreasuryBlockHeight(nHeight))? heightValidator_.GetTreasuryBlockPaymentCycle(nHeight):0;
    *const_cast<CAmount*>(&rewards.nCharityReward) *= (heightValidator_.IsValidTreasuryBlockHeight(nHeight))? heightValidator_.GetTreasuryBlockPaymentCycle(nHeight):0;
    *const_cast<CAmount*>(&rewards.nLotteryReward) *= (heightValidator_.IsValidLotteryBlockHeight(nHeight))? heightValidator_.GetLotteryBlockPaymentCycle(nHeight):0;
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