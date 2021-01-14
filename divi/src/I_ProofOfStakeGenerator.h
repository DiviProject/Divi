#ifndef I_PROOF_OF_STAKE_GENERATOR_H
#define I_PROOF_OF_STAKE_GENERATOR_H
class StakingData;
class uint256;

enum HashproofGenerationState
{
    FAILED_SETUP,
    FAILED_GENERATION,
    SUCCESS
};
class HashproofCreationResult
{
private:
    HashproofCreationResult(unsigned timestamp, HashproofGenerationState status);
    const unsigned hashproofTimestamp_;
    HashproofGenerationState state_;
public:
    static HashproofCreationResult Success(unsigned timestamp);
    static HashproofCreationResult FailedGeneration();
    static HashproofCreationResult FailedSetup();
    bool succeeded() const;
    bool failedAtSetup() const;
    const unsigned& timestamp() const;
};

class I_ProofOfStakeGenerator
{
public:
    static const int nHashDrift = 45;
    virtual ~I_ProofOfStakeGenerator(){}
    virtual HashproofCreationResult CreateHashproofTimestamp(
        const StakingData& stakingData,
        const unsigned initialTimestamp) const = 0;
    virtual bool ComputeAndVerifyProofOfStake(
        const StakingData& stakingData,
        const unsigned int& hashproofTimestamp,
        uint256& hashProofOfStake) const = 0;
};
#endif// I_PROOF_OF_STAKE_GENERATOR_H