#ifndef STAKABLE_COIN_H
#define STAKABLE_COIN_H
#include <uint256.h>
#include <primitives/transaction.h>
struct StakableCoin
{
    const CTransaction* tx;
    unsigned outputIndex;
    uint256 blockHashOfFirstConfirmation;

    StakableCoin(
        ): tx(nullptr)
        , outputIndex(0u)
        , blockHashOfFirstConfirmation(0u)
    {
    }

    StakableCoin(
        const CTransaction* txIn,
        unsigned outputIndexIn,
        uint256 blockHashIn
        ): tx(txIn)
        , outputIndex(outputIndexIn)
        , blockHashOfFirstConfirmation(blockHashIn)
    {
    }
    bool operator<(const StakableCoin& other) const
    {
        if(!tx && !other.tx)
        {
            return true;
        }
        if(!tx)
        {
            return true;
        }
        if(!other.tx)
        {
            return false;
        }
        const COutPoint left(tx->GetHash(),outputIndex);
        const COutPoint right(other.tx->GetHash(),other.outputIndex);
        return  left < right;
    }
};
#endif//STAKABLE_COIN_H