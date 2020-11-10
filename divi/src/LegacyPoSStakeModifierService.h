#ifndef LEGACY_POS_STAKE_MODIFIER_SERVICE_H
#define LEGACY_POS_STAKE_MODIFIER_SERVICE_H
#include <stdint.h>
#include <I_PoSStakeModifierService.h>
#include <uint256.h>
class BlockMap;
class CChain;
class LegacyPoSStakeModifierService: public I_PoSStakeModifierService
{
private:
    const BlockMap& blockIndexByHash_;
    const CChain& activeChain_;

    uint64_t GetKernelStakeModifier(uint256 hashBlockFrom) const;
public:
    LegacyPoSStakeModifierService(const BlockMap& blockIndexByHash, const CChain& activeChain);
    virtual std::pair<uint64_t,bool> getStakeModifier(const uint256& blockHash) const;
};
#endif// LEGACY_POS_STAKE_MODIFIER_SERVICE_H