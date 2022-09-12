// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <stdint.h>
#include <map>
#include <uint256.h>

class CBlockHeader;
class CBlockIndex;
class uint256;
class arith_uint256;
class CChainParams;

// Define difficulty retarget algorithms
enum DiffMode {
    DIFF_DEFAULT = 0, // Default to invalid 0
    DIFF_BTC = 1,     // Retarget every x blocks (Bitcoin style)
    DIFF_KGW = 2,     // Retarget using Kimoto Gravity Well
    DIFF_DGW = 3,     // Retarget using Dark Gravity Wave v3
};

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CChainParams& chainParameters);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const CChainParams& chainParameters);
uint256 GetBlockProof(const CBlockIndex& block);

class I_ProofOfStakeGenerator;
class CBlock;
class BlockMap;
class Settings;
class CChain;

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(
    const I_ProofOfStakeGenerator& posGenerator,
    const Settings& settings,
    const BlockMap& mapBlockIndex,
    const CBlock& block,
    CBlockIndex* pindexPrev,
    uint256& hashProofOfStake);

bool CheckWork(
    const CChainParams& chainParameters,
    const I_ProofOfStakeGenerator& posGenerator,
    const BlockMap& blockIndicesByHash,
    const Settings& settings,
    const CBlock& block,
    uint256& hashProofOfStake,
    CBlockIndex* const pindexPrev);
#endif // BITCOIN_POW_H
