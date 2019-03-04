// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KERNEL_H
#define BITCOIN_KERNEL_H

#include <validation.h>

// MODIFIER_INTERVAL: time to elapse before new modifier is computed
static const unsigned int MODIFIER_INTERVAL = 60;
static const unsigned int MODIFIER_INTERVAL_TESTNET = 60;
static const unsigned int MAX_KERNEL_COMBINED_INPUTS = 20;
static const unsigned int MIN_STAKING_AMOUNT = 10000;
extern unsigned int nModifierInterval;
extern unsigned int getIntervalVersion(bool fTestNet);

// MODIFIER_INTERVAL_RATIO:
// ratio of group interval length between the last group and the first group
static const int MODIFIER_INTERVAL_RATIO = 3;

bool ComputeAndSetStakeModifier(CBlockIndex *pindexNew, const Consensus::Params &consensus);

// Check whether stake kernel meets hash target
// Sets hashProofOfStake on success return
bool CheckStakeKernelHash(CBlockIndex *pindexPrev,
                          unsigned int nBits,
                          const CBlock &blockFrom,
                          const CTransaction &txPrev,
                          const COutPoint prevout,
                          unsigned int& nTimeTx,
                          unsigned int nHashDrift,
                          bool fCheck,
                          uint256& hashProofOfStake,
                          bool fPrintProofOfStake = false);

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(const CBlock &block, uint256& hashProofOfStake);

// Check whether the coinstake timestamp meets protocol
bool CheckCoinStakeTimestamp(int64_t nTimeBlock, int64_t nTimeTx);

// Get stake modifier checksum
uint64_t GetStakeModifierChecksum(const CBlockIndex* pindex);

// Get time weight using supplied timestamps
int64_t GetWeight(int64_t nIntervalBeginning, int64_t nIntervalEnd);

#endif // BITCOIN_KERNEL_H
