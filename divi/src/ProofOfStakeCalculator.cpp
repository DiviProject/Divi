#include <ProofOfStakeCalculator.h>
#include <streams.h>
#include <amount.h>
#include <primitives/transaction.h>
#include <hash.h>

static constexpr unsigned int MAXIMUM_COIN_AGE_WEIGHT_FOR_STAKING = 60 * 60 * 24 * 7 - 60 * 60;

static uint256 stakeHash(uint64_t stakeModifier, unsigned int hashproofTimestamp, const COutPoint& prevout, unsigned int coinstakeStartTime)
{
    //Divi will hash in the transaction hash and the index number in order to make sure each hash is unique
    CDataStream ss(SER_GETHASH, 0);
    ss << stakeModifier << coinstakeStartTime << prevout.n << prevout.hash << hashproofTimestamp;
    return Hash(ss.begin(), ss.end());
}

//test hash vs target
static bool stakeTargetHit(const uint256& hashProofOfStake, int64_t nValueIn, const uint256& bnTargetPerCoinDay, int64_t nTimeWeight)
{
    const uint256 bnCoinDayWeight = (uint256(nValueIn) * nTimeWeight) / COIN / 400;

    uint256 target = bnTargetPerCoinDay;
    if (!target.MultiplyBy(bnCoinDayWeight)) {
        // In regtest with minimal difficulty, it may happen that the
        // modification overflows the uint256, in which case it just means
        // that the target will always be hit.
        return true;
    }

    // Now check if proof-of-stake hash meets target protocol
    return hashProofOfStake < target;
}


ProofOfStakeCalculator::ProofOfStakeCalculator(
    const COutPoint& utxoToStake,
    const int64_t& utxoValue,
    const uint64_t& stakeModifier,
    unsigned int blockDifficultyBits,
    unsigned int coinstakeStartTime
    ): utxoToStake_(utxoToStake)
    , utxoValue_(utxoValue)
    , stakeModifier_(stakeModifier)
    , targetPerCoinDay_(uint256().SetCompact(blockDifficultyBits))
    , coinstakeStartTime_(coinstakeStartTime)
{
}

bool ProofOfStakeCalculator::computeProofOfStakeAndCheckItMeetsTarget(
    unsigned int hashproofTimestamp,
    uint256& computedProofOfStake,
    bool checkOnly) const
{
    if(!checkOnly) computedProofOfStake = stakeHash(stakeModifier_,hashproofTimestamp, utxoToStake_,coinstakeStartTime_);
    int64_t coinAgeWeightOfUtxo = std::min<int64_t>(hashproofTimestamp - coinstakeStartTime_, MAXIMUM_COIN_AGE_WEIGHT_FOR_STAKING);
    return stakeTargetHit(computedProofOfStake,utxoValue_,targetPerCoinDay_, coinAgeWeightOfUtxo);
}