#ifndef MOCK_POS_STAKE_MODIFIER_SERVICE_h
#define MOCK_POS_STAKE_MODIFIER_SERVICE_h
#include <I_PoSStakeModifierService.h>
#include <gmock/gmock.h>
class MockPoSStakeModifierService: public I_PoSStakeModifierService
{
public:
    typedef std::pair<uint64_t,bool> StakeModifierAndFoundStatusPair;
    MOCK_CONST_METHOD1(getStakeModifier, StakeModifierAndFoundStatusPair(const StakingData&) );
};
#endif// MOCK_POS_STAKE_MODIFIER_SERVICE_h
