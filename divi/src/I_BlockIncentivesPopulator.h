#ifndef I_BLOCK_INCENTIVES_POPULATOR_H
#define _BLOCK_INCENTIVES_POPULATOR_H

#include <amount.h>

class CMutableTransaction;
class CBlockRewards;
class CBlockIndex;
class CTransaction;

class I_BlockIncentivesPopulator
{
public:
    I_BlockIncentivesPopulator() = default;
    virtual ~I_BlockIncentivesPopulator() = default;

    virtual void FillBlockPayee(CMutableTransaction& txNew, const CBlockRewards &payments, const CBlockIndex* chainTip, bool fProofOfStake) const = 0;
    virtual bool IsBlockValueValid(const CBlockRewards &nExpectedValue, CAmount nMinted, int nHeight) const = 0;
    virtual bool HasValidPayees(const CTransaction &txNew, const CBlockIndex* pindex) const = 0;
};

#endif // I_BLOCK_INCENTIVES_POPULATOR_H
