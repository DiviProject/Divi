#ifndef I_PROOF_OF_STAKE_CALCULATOR_H
#define I_PROOF_OF_STAKE_CALCULATOR_H
class uint256;
class I_ProofOfStakeCalculator
{
public:
    virtual ~I_ProofOfStakeCalculator(){}
    virtual bool computeProofOfStakeAndCheckItMeetsTarget(
        unsigned int hashproofTimestamp,
        uint256& computedProofOfStake,
        bool checkOnly = false) const = 0;
};
#endif// I_PROOF_OF_STAKE_CALCULATOR_H