#ifndef I_POS_STAKE_MODIFIER_SERVICE_H
#define I_POS_STAKE_MODIFIER_SERVICE_H
#include <utility>
#include <stdint.h>
class StakingData;
class I_PoSStakeModifierService
{
public:
    virtual ~I_PoSStakeModifierService(){}
    virtual std::pair<uint64_t,bool> getStakeModifier(const StakingData& stakingData) const = 0;
};
#endif// I_POS_STAKE_MODIFIER_SERVICE_H
