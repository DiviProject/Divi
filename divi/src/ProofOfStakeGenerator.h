#ifndef PROOF_OF_STAKE_GENERATOR_H
#define PROOF_OF_STAKE_GENERATOR_H
#include <memory>
#include <I_ProofOfStakeGenerator.h>

class StakingData;
class uint256;
class I_PoSStakeModifierService;
class I_ProofOfStakeCalculator;

class ProofOfStakeGenerator: public I_ProofOfStakeGenerator
{
private:
    const I_PoSStakeModifierService& stakeModifierService_;
    unsigned minimumCoinAgeForStaking_;

    bool ProofOfStakeTimeRequirementsAreMet(
        unsigned int coinstakeStartTime,
        unsigned int hashproofTimestamp) const;
    bool CreateProofOfStakeCalculator(
        const StakingData& stakingData,
        const unsigned& initialHashproofTimestamp,
        std::shared_ptr<I_ProofOfStakeCalculator>& calculator) const;
public:
    ProofOfStakeGenerator(
        const I_PoSStakeModifierService& stakeModifierService,
        unsigned minimumCoinAgeForStaking);
    HashproofCreationResult createHashproofTimestamp(
        const StakingData& stakingData,
        const unsigned initialTimestamp) const;
    bool computeAndVerifyProofOfStake(
        const StakingData& stakingData,
        const unsigned int& hashproofTimestamp,
        uint256& hashProofOfStake) const;
};
#endif// PROOF_OF_STAKE_GENERATOR_H