#ifndef PROOF_OF_STAKE_GENERATOR_H
#define PROOF_OF_STAKE_GENERATOR_H
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

HashproofCreationResult CreateHashproofTimestamp(
    const StakingData& stakingData,
    const unsigned initialTimestamp);
bool ComputeAndVerifyProofOfStake(
    const StakingData& stakingData,
    const unsigned int& hashproofTimestamp,
    uint256& hashProofOfStake);
#endif// PROOF_OF_STAKE_GENERATOR_H