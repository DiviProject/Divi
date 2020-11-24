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
    uint256 blockHashOfChainTipBlock_;

    StakingData() = default;
    StakingData(
        unsigned int nBits,
        unsigned int blockTimeOfFirstConfirmationBlock,
        uint256 blockHashOfFirstConfirmationBlock,
        COutPoint utxoBeingStaked,
        CAmount utxoValue,
        uint256 blockHashOfChainTipBlock
        ): nBits_(nBits)
        , blockTimeOfFirstConfirmationBlock_(blockTimeOfFirstConfirmationBlock)
        , blockHashOfFirstConfirmationBlock_(blockHashOfFirstConfirmationBlock)
        , utxoBeingStaked_(utxoBeingStaked)
        , utxoValue_(utxoValue)
        , blockHashOfChainTipBlock_(blockHashOfChainTipBlock)
    {
    }
    bool operator==(const StakingData& other) const
    {
        return nBits_== other.nBits_ &&
            blockTimeOfFirstConfirmationBlock_== other.blockTimeOfFirstConfirmationBlock_ &&
            blockHashOfFirstConfirmationBlock_== other.blockHashOfFirstConfirmationBlock_ &&
            utxoBeingStaked_== other.utxoBeingStaked_ &&
            utxoValue_== other.utxoValue_ &&
            blockHashOfChainTipBlock_== other.blockHashOfChainTipBlock_;
    }
};
#endif// STAKING_DATA_H