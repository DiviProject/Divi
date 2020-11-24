#include <PoSStakeModifierService.h>
#include <uint256.h>
#include <StakingData.h>
#include <blockmap.h>

PoSStakeModifierService::PoSStakeModifierService(
    const BlockMap& blockIndexByHash,
    const CChain& activeChain
    ): blockIndexByHash_(blockIndexByHash)
    , activeChain_(activeChain)
{
}

std::pair<uint64_t,bool> PoSStakeModifierService::getStakeModifier(const StakingData& stakingData) const
{
    return std::pair<uint64_t,bool>();
}