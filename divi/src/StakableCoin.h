#ifndef STAKABLE_COIN_H
#define STAKABLE_COIN_H
#include <uint256.h>
#include <primitives/transaction.h>
struct StakableCoin
{
    const CTransaction* tx;
    COutPoint utxo;
    uint256 blockHashOfFirstConfirmation;

    explicit StakableCoin(
        const CTransaction& txIn,
        const COutPoint& utxoIn,
        uint256 blockHashIn
        ): tx(&txIn)
        , blockHashOfFirstConfirmation(blockHashIn)
        , utxo(utxoIn)
    {
    }

    bool operator<(const StakableCoin& other) const
    {
        return utxo < other.utxo;
    }

    /** Convenience method to access the staked tx out.  */
    const CTxOut& GetTxOut() const
    {
        return tx->vout[utxo.n];
    }

};
#endif//STAKABLE_COIN_H
