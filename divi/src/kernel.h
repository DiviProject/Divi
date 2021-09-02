// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KERNEL_H
#define BITCOIN_KERNEL_H

#include <stdint.h>
#include <amount.h>
#include <map>
class CBlockIndex;
class CBlockRewards;
class CBlock;
class CCoinsViewCache;
class CTransaction;
class uint256;
class I_ProofOfStakeGenerator;
class BlockMap;
class Settings;

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(
    const I_ProofOfStakeGenerator& posGenerator,
    const Settings& settings,
    const BlockMap& mapBlockIndex,
    const CBlock& block,
    CBlockIndex* pindexPrev,
    uint256& hashProofOfStake);

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(
    int nHeight,
    unsigned int nStakeModifierChecksum);

void SetStakeModifiersForNewBlockIndex(const BlockMap& mapBlockIndex,CBlockIndex* pindexNew);

#endif // BITCOIN_KERNEL_H
