#ifndef PROOF_OF_STAKE_CALCULATOR_H
#define PROOF_OF_STAKE_CALCULATOR_H
#include <stdint.h>
#include <uint256.h>
#include <I_ProofOfStakeCalculator.h>
class COutPoint;
class ProofOfStakeCalculator: public I_ProofOfStakeCalculator
{
private:
    const COutPoint& utxoToStake_;
    const int64_t& utxoValue_;
    const uint64_t& stakeModifier_;
    const uint256 targetPerCoinDay_;
    const unsigned int coinstakeStartTime_;
public:
    ProofOfStakeCalculator(
        const COutPoint& utxoToStake,
        const int64_t& utxoValue,
        const uint64_t& stakeModifier,
        unsigned int blockDifficultyBits,
        unsigned int coinstakeStartTime);

    virtual bool computeProofOfStakeAndCheckItMeetsTarget(
        unsigned int hashproofTimestamp,
        uint256& computedProofOfStake,
        bool checkOnly = false) const;
};
#endif// PROOF_OF_STAKE_CALCULATOR_H
