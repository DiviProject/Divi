#include <PoSStakeModifierService.h>
#include <uint256.h>
#include <StakingData.h>
#include <blockmap.h>

PoSStakeModifierService::PoSStakeModifierService(
    const I_PoSStakeModifierService& decorated,
    const BlockMap& blockIndexByHash,
    const CChain& activeChain
    ): decoratedStakeModifierService_(decorated)
    , blockIndexByHash_(blockIndexByHash)
    , activeChain_(activeChain)
{
}

std::pair<uint64_t,bool> PoSStakeModifierService::getStakeModifier(const StakingData& stakingData) const
{
    if(blockIndexByHash_.count(stakingData.blockHashOfChainTipBlock_) == 0)
    {
        return std::pair<uint64_t,bool>();
    }
    CBlockIndex* nominalChainTip = blockIndexByHash_.find(stakingData.blockHashOfChainTipBlock_)->second;
    if(nominalChainTip->GetBlockTime() > 1609459199)
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