#ifndef POS_STAKE_MODIFIER_SERVICE_H
#define POS_STAKE_MODIFIER_SERVICE_H
#include <stdint.h>
#include <I_PoSStakeModifierService.h>

class StakingData;
class BlockMap;
class CChain;
class PoSStakeModifierService: public I_PoSStakeModifierService
{
private:
    const BlockMap& blockIndexByHash_;
    const CChain& activeChain_;
public:
    PoSStakeModifierService(const BlockMap& blockIndexByHash, const CChain& activeChain);
    virtual std::pair<uint64_t,bool> getStakeModifier(const StakingData& stakingData) const;
};
#endif// POS_STAKE_MODIFIER_SERVICE_H