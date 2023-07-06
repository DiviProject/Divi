#include <ProofOfStakeCalculator.h>
#include <streams.h>
#include <amount.h>
#include <primitives/transaction.h>
#include <hash.h>
#include <StakingData.h>

static constexpr unsigned int MAXIMUM_COIN_AGE_WEIGHT_FOR_STAKING = 60 * 60 * 24 * 7 - 60 * 60;

static uint256 stakeHash(uint64_t stakeModifier, unsigned int hashproofTimestamp, const COutPoint& prevout, unsigned int coinstakeStartTime)
{
    //Divi will hash in the transaction hash and the index number in order to make sure each hash is unique
    CDataStream ss(SER_GETHASH, 0);
    ss << stakeModifier << coinstakeStartTime << prevout.n << prevout.hash << hashproofTimestamp;
    return Hash(ss.begin(), ss.end());
}

//test hash vs target
static bool stakeTargetHit(const uint256& hashProofOfStake, int64_t nValueIn, const uint256& coinAgeTarget, int64_t nTimeWeight)
{
    const uint256 coinAgeWeight = (uint256(nValueIn) * nTimeWeight) / COIN / 400;

    uint256 target = coinAgeTarget;
    if (!target.MultiplyBy(coinAgeWeight)) {
        // In regtest with minimal difficulty, it may happen that the
        // modification overflows the uint256, in which case it just means
        // that the target will always be hit.
        return true;
    }

    // Now check if proof-of-stake hash meets target protocol
    return hashProofOfStake < target;
}


ProofOfStakeCalculator::ProofOfStakeCalculator(
    const StakingData& stakingData,
    const uint64_t stakeModifier
    ): utxoToStake_(stakingData.utxoBeingStaked_)
    , utxoValue_(stakingData.utxoValue_)
    , stakeModifier_(stakeModifier)
    , coinAgeTarget_(uint256().SetCompact(stakingData.nBits_))
    , coinstakeStartTime_(stakingData.blockTimeOfFirstConfirmationBlock_)
{
}

bool ProofOfStakeCalculator::computeProofOfStakeAndCheckItMeetsTarget(
    unsigned int hashproofTimestamp,
    uint256& computedProofOfStake,
    bool checkOnly) const
{
    if(!checkOnly) computedProofOfStake = stakeHash(stakeModifier_,hashproofTimestamp, utxoToStake_,coinstakeStartTime_);
    int64_t coinAgeWeightOfUtxo = std::min<int64_t>(hashproofTimestamp - coinstakeStartTime_, MAXIMUM_COIN_AGE_WEIGHT_FOR_STAKING);
    return stakeTargetHit(computedProofOfStake,utxoValue_,coinAgeTarget_, coinAgeWeightOfUtxo);
}