#ifndef STAKING_DATA_H
#define STAKING_DATA_H
#include <uint256.h>
#include <primitives/transaction.h>
#include <amount.h>
struct StakingData
{
    unsigned int nBits_;
    unsigned int blockTimeOfFirstConfirmationBlock_;
    uint256 blockHashOfFirstConfirmationBlock_;
    COutPoint utxoBeingStaked_;
    CAmount utxoValue_;

    StakingData() = default;
    StakingData(
        unsigned int nBits,
        unsigned int blockTimeOfFirstConfirmationBlock,
        uint256 blockHashOfFirstConfirmationBlock,
        COutPoint utxoBeingStaked,
        CAmount utxoValue
        ): nBits_(nBits)
        , blockTimeOfFirstConfirmationBlock_(blockTimeOfFirstConfirmationBlock)
        , blockHashOfFirstConfirmationBlock_(blockHashOfFirstConfirmationBlock)
        , utxoBeingStaked_(utxoBeingStaked)
        , utxoValue_(utxoValue)
    {
    }
};
#endif// STAKING_DATA_H