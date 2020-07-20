// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KERNEL_H
#define BITCOIN_KERNEL_H

#include <stdint.h>
#include <uint256.h>
#include <primitives/transaction.h>
#include <primitives/block.h>
#include <streams.h>
class CBlockIndex;

static const unsigned int MAX_KERNEL_COMBINED_INPUTS = 20;

// Compute the hash modifier for proof-of-stake
bool ComputeNextStakeModifier(
    const CBlockIndex* pindexPrev,
    uint64_t& nStakeModifier,
    bool& fGeneratedStakeModifier);

// Check whether stake kernel meets hash target
// Sets hashProofOfStake on success return
uint256 stakeHash(
    unsigned int nTimeTx,
    CDataStream ss,
    unsigned int prevoutIndex,
    uint256 prevoutHash,
    unsigned int nTimeBlockFrom);
bool stakeTargetHit(
    uint256 hashProofOfStake,
    int64_t nValueIn,
    uint256 bnTargetPerCoinDay,
    int64_t nTimeWeight);
bool CheckStakeKernelHash(
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps,
    unsigned int nBits,
    const CBlock blockFrom,
    const CTransaction txPrev,
    const COutPoint prevout,
    unsigned int& nTimeTx,
    unsigned int nHashDrift,
    bool fCheck,
    uint256& hashProofOfStake,
    bool fPrintProofOfStake = false);

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(
    const CBlock block,
    uint256& hashProofOfStake);

// Get stake modifier checksum
unsigned int GetStakeModifierChecksum(
    const CBlockIndex* pindex);

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(
    int nHeight,
    unsigned int nStakeModifierChecksum);

#endif // BITCOIN_KERNEL_H
