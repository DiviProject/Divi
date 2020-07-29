// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KERNEL_H
#define BITCOIN_KERNEL_H

#include <stdint.h>
#include <uint256.h>
#include <streams.h>
#include <amount.h>
class CBlockIndex;
class CBlock;
class CTransaction;
class COutPoint;

static const unsigned int MAX_KERNEL_COMBINED_INPUTS = 20;

class I_ProofOfStakeCalculator
{
public:
    virtual ~I_ProofOfStakeCalculator(){}
    virtual bool computeProofOfStakeAndCheckItMeetsTarget(
        unsigned int nTimeTx,
        unsigned int nTimeBlockFrom,
        uint256& computedProofOfStake,
        bool checkOnly = false) const = 0;
};
class ProofOfStakeCalculator: public I_ProofOfStakeCalculator
{
private:
    const COutPoint& utxoToStake_;
    const int64_t& utxoValue_;
    const uint64_t& stakeModifier_;
    const uint256 targetPerCoinDay_;
public:
    ProofOfStakeCalculator(
        const COutPoint& utxoToStake,
        const int64_t& utxoValue,
        const uint64_t& stakeModifier,
        unsigned int blockDifficultyBits);

    virtual bool computeProofOfStakeAndCheckItMeetsTarget(
        unsigned int nTimeTx,
        unsigned int nTimeBlockFrom,
        uint256& computedProofOfStake,
        bool checkOnly = false) const;
};


bool CreateHashProofForProofOfStake(
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps,
    unsigned int nBits,
    const CBlock& blockFrom,
    const COutPoint& prevout,
    const CAmount& utxoValue,
    unsigned int& nTimeTx,
    bool fCheck,
    uint256& hashProofOfStake,
    bool fPrintProofOfStake = false);

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(
    const CBlock& block,
    uint256& hashProofOfStake);

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(
    int nHeight,
    unsigned int nStakeModifierChecksum);

void SetStakeModifiersForNewBlockIndex(CBlockIndex* pindexNew);

#endif // BITCOIN_KERNEL_H
