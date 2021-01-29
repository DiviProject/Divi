#ifndef MOCK_BLOCK_INCENTIVES_POPULATOR_H
#define MOCK_BLOCK_INCENTIVES_POPULATOR_H

#include "BlockRewards.h"
#include "I_BlockIncentivesPopulator.h"

#include <gmock/gmock.h>

class MockBlockIncentivesPopulator : public I_BlockIncentivesPopulator
{
public:
    MOCK_CONST_METHOD4(FillBlockPayee, void(CMutableTransaction&, const CBlockRewards&, const CBlockIndex*, bool fProofOfStake));
    MOCK_CONST_METHOD3(IsBlockValueValid, bool(const CBlockRewards&, CAmount, int));
    MOCK_CONST_METHOD2(HasValidPayees, bool(const CTransaction&, const CBlockIndex*));
};

#endif // MOCK_BLOCK_INCENTIVES_POPULATOR_H
