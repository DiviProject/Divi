#ifndef PROOF_OF_STAKE_GENERATOR_H
#define PROOF_OF_STAKE_GENERATOR_H
class StakingData;
class uint256;
class HashproofCreationResult
{
private:
    HashproofCreationResult(unsigned timestamp, bool status);
    const unsigned hashproofTimestamp;
    const bool prerequisitsWereMetForGeneration;
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