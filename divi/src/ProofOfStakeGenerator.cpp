#include <ProofOfStakeGenerator.h>

#include <uint256.h>
#include <Logging.h>
#include <StakingData.h>
#include <LegacyPoSStakeModifierService.h>
#include <PoSStakeModifierService.h>
#include <chain.h>
#include <chainparams.h>
#include <blockmap.h>
#include <ProofOfStakeCalculator.h>
#include <memory>

extern BlockMap mapBlockIndex;
extern CChain chainActive;
extern const int nHashDrift = 45;

// Start of Proof-of-Stake Computations
HashproofCreationResult::HashproofCreationResult(
    unsigned timestamp,
    HashproofGenerationState state
    ): hashproofTimestamp_(timestamp)
    , state_(state)
{
}
HashproofCreationResult HashproofCreationResult::Success(unsigned timestamp)
{
    return HashproofCreationResult(timestamp, SUCCESS);
}
HashproofCreationResult HashproofCreationResult::FailedGeneration()
{
    return HashproofCreationResult(0, FAILED_GENERATION);
}
HashproofCreationResult HashproofCreationResult::FailedSetup()
{
    return HashproofCreationResult(0, FAILED_SETUP);
}
bool HashproofCreationResult::succeeded() const
{
    return state_==SUCCESS;
}
bool HashproofCreationResult::failedAtSetup() const
{
    return state_==FAILED_SETUP;
}
const unsigned& HashproofCreationResult::timestamp() const
{
    return hashproofTimestamp_;
}


//instead of looping outside and reinitializing variables many times, we will give a hashproofTimestamp and also search interval so that we can do all the hashing here
bool CreateHashProofForProofOfStake(
    const I_ProofOfStakeCalculator& calculator,
    const StakingData& stakingData,
    unsigned int& hashproofTimestamp)
{
    uint256 hashproof = 0;
    for (unsigned int i = 0; i < nHashDrift; i++) //iterate the hashing
    {
        if(!calculator.computeProofOfStakeAndCheckItMeetsTarget(hashproofTimestamp,hashproof,false))
        {
            --hashproofTimestamp;
            continue;
        }
        else
        {
            return true;
        }
    }

    return false;
}

ProofOfStakeGenerator::ProofOfStakeGenerator(
    const I_PoSStakeModifierService& stakeModifierService,
    unsigned minimumCoinAgeForStaking
    ): stakeModifierService_(stakeModifierService)
    , minimumCoinAgeForStaking_(minimumCoinAgeForStaking)
{
}

bool ProofOfStakeGenerator::ProofOfStakeTimeRequirementsAreMet(
    unsigned int coinstakeStartTime,
    unsigned int hashproofTimestamp) const
{
    if (hashproofTimestamp < coinstakeStartTime) // Transaction timestamp violation
    {
        return error("%s : nTime violation", __func__);
    }

    if (coinstakeStartTime + minimumCoinAgeForStaking_ > hashproofTimestamp) // Min age requirement
    {
        return error("%s : min age violation - coinstakeStartTime=%d minimum coinage=%d hashproofTimestamp=%d",
            __func__, coinstakeStartTime, minimumCoinAgeForStaking_, hashproofTimestamp);
    }
    return true;
}

bool ProofOfStakeGenerator::CreateProofOfStakeCalculator(
    const StakingData& stakingData,
    const unsigned& initialHashproofTimestamp,
    std::shared_ptr<I_ProofOfStakeCalculator>& calculator) const
{
    if(!ProofOfStakeTimeRequirementsAreMet(stakingData.blockTimeOfFirstConfirmationBlock_,initialHashproofTimestamp))
        return false;

    std::pair<uint64_t,bool> stakeModifierData = stakeModifierService_.getStakeModifier(stakingData);
    if (!stakeModifierData.second)
    {
        return error("%s: failed to get kernel stake modifier \n",__func__);
    }
    calculator = std::make_shared<ProofOfStakeCalculator>(stakingData, stakeModifierData.first);

    if(!calculator.get())
        return false;

    return true;
}

bool ProofOfStakeGenerator::ComputeAndVerifyProofOfStake(
    const StakingData& stakingData,
    const unsigned int& hashproofTimestamp,
    uint256& hashProofOfStake) const
{
    std::shared_ptr<I_ProofOfStakeCalculator> calculator;
    if(!CreateProofOfStakeCalculator(stakingData,hashproofTimestamp,calculator))
        return false;
    return calculator->computeProofOfStakeAndCheckItMeetsTarget(
        hashproofTimestamp, hashProofOfStake,false);
}
HashproofCreationResult ProofOfStakeGenerator::CreateHashproofTimestamp(
    const StakingData& stakingData,
    const unsigned initialTimestamp) const
{
    std::shared_ptr<I_ProofOfStakeCalculator> calculator;
    if(!CreateProofOfStakeCalculator(stakingData,initialTimestamp,calculator))
        return HashproofCreationResult::FailedSetup();

    unsigned hashproofTimestamp = initialTimestamp;
    if(!CreateHashProofForProofOfStake(
        *calculator,
        stakingData,
        hashproofTimestamp))
    {
        return HashproofCreationResult::FailedGeneration();
    }

    return HashproofCreationResult::Success(hashproofTimestamp);
}