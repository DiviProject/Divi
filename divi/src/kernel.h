// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KERNEL_H
#define BITCOIN_KERNEL_H

#include <stdint.h>
#include <uint256.h>
#include <amount.h>
#include <map>
#include <I_PoSStakeModifierService.h>
#include <I_ProofOfStakeCalculator.h>
class CBlockIndex;
class CBlock;
class CTransaction;
class COutPoint;
class BlockMap;
class CChain;
struct StakingData;

static const unsigned int MAX_KERNEL_COMBINED_INPUTS = 20;

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

bool CreateHashProofForProofOfStake(
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps,
    const StakingData& stakingData,
    unsigned int& hashproofTimestamp,
    uint256& hashProofOfStake);

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(
    const CBlock& block,
    int blockHeight,
    uint256& hashProofOfStake);

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(
    int nHeight,
    unsigned int nStakeModifierChecksum);

void SetStakeModifiersForNewBlockIndex(CBlockIndex* pindexNew);

#endif // BITCOIN_KERNEL_H
