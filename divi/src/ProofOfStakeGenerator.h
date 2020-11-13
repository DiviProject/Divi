#ifndef PROOF_OF_STAKE_GENERATOR_H
#define PROOF_OF_STAKE_GENERATOR_H
#include <memory>

class StakingData;
class uint256;
class I_PoSStakeModifierService;
class I_ProofOfStakeCalculator;

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

class ProofOfStakeGenerator
{
private:
    const I_PoSStakeModifierService& stakeModifierService_;
    bool ProofOfStakeTimeRequirementsAreMet(
        unsigned int coinstakeStartTime,
        unsigned int hashproofTimestamp) const;
    bool CreateProofOfStakeCalculator(
        const StakingData& stakingData,
        const unsigned& initialHashproofTimestamp,
        std::shared_ptr<I_ProofOfStakeCalculator>& calculator) const;
public:
    ProofOfStakeGenerator(const I_PoSStakeModifierService& stakeModifierService);
    HashproofCreationResult CreateHashproofTimestamp(
        const StakingData& stakingData,
        const unsigned initialTimestamp);
    bool ComputeAndVerifyProofOfStake(
        const StakingData& stakingData,
        const unsigned int& hashproofTimestamp,
        uint256& hashProofOfStake);
};
HashproofCreationResult CreateHashproofTimestamp(
    const StakingData& stakingData,
    const unsigned initialTimestamp);
bool ComputeAndVerifyProofOfStake(
    const StakingData& stakingData,
    const unsigned int& hashproofTimestamp,
    uint256& hashProofOfStake);
#endif// PROOF_OF_STAKE_GENERATOR_H