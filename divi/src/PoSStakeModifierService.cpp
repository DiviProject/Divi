#include <PoSStakeModifierService.h>
#include <uint256.h>
#include <StakingData.h>
#include <blockmap.h>
#include <ForkActivation.h>


PoSStakeModifierService::PoSStakeModifierService(
    const I_PoSStakeModifierService& decorated,
    const BlockMap& blockIndexByHash
    ): decoratedStakeModifierService_(decorated)
    , blockIndexByHash_(blockIndexByHash)
{
}

std::pair<uint64_t,bool> PoSStakeModifierService::getStakeModifier(const StakingData& stakingData) const
{
    if(blockIndexByHash_.count(stakingData.blockHashOfChainTipBlock_) == 0)
    {
        return std::pair<uint64_t,bool>();
    }

    CBlockIndex* nominalChainTip = blockIndexByHash_.find(stakingData.blockHashOfChainTipBlock_)->second;
    if(ActivationState(nominalChainTip).IsActive(Fork::HardenedStakeModifier))
    {
        while(nominalChainTip && !nominalChainTip->GeneratedStakeModifier())
        {
            nominalChainTip = nominalChainTip->pprev;
        }
        if(!nominalChainTip || !nominalChainTip->GeneratedStakeModifier())
        {
            return std::pair<uint64_t,bool>();
        }
        else
        {
            return std::make_pair(nominalChainTip->nStakeModifier,true);
        }

    }
    return decoratedStakeModifierService_.getStakeModifier(stakingData);
}