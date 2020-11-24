#ifndef LEGACY_POS_STAKE_MODIFIER_SERVICE_H
#define LEGACY_POS_STAKE_MODIFIER_SERVICE_H
#include <stdint.h>
#include <I_PoSStakeModifierService.h>

class uint256;
class StakingData;
class BlockMap;
class CChain;
class LegacyPoSStakeModifierService: public I_PoSStakeModifierService
{
private:
    const BlockMap& blockIndexByHash_;
    const CChain& activeChain_;

    uint64_t GetKernelStakeModifier(const uint256& hashBlockFrom) const;
public:
    LegacyPoSStakeModifierService(const BlockMap& blockIndexByHash, const CChain& activeChain);
    virtual std::pair<uint64_t,bool> getStakeModifier(const StakingData& stakinData) const;
};
#endif// LEGACY_POS_STAKE_MODIFIER_SERVICE_H