#ifndef POS_STAKE_MODIFIER_SERVICE_H
#define POS_STAKE_MODIFIER_SERVICE_H
#include <stdint.h>
#include <I_PoSStakeModifierService.h>

class StakingData;
class BlockMap;
class PoSStakeModifierService: public I_PoSStakeModifierService
{
private:
    const I_PoSStakeModifierService& decoratedStakeModifierService_;
    const BlockMap& blockIndexByHash_;
public:
    PoSStakeModifierService(const I_PoSStakeModifierService& decorated, const BlockMap& blockIndexByHash);
    virtual std::pair<uint64_t,bool> getStakeModifier(const StakingData& stakingData) const;
};
#endif// POS_STAKE_MODIFIER_SERVICE_H