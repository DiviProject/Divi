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
        return blockHashOfFirstConfirmation < other.blockHashOfFirstConfirmation;
    }
};
#endif//STAKABLE_COIN_H