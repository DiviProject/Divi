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
class CBlockIndex;
class CBlock;
class CTransaction;
class COutPoint;
class BlockMap;
class CChain;
struct StakingData;

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(
    const CBlock& block,
    CBlockIndex* pindexPrev,
    uint256& hashProofOfStake);

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(
    int nHeight,
    unsigned int nStakeModifierChecksum);

void SetStakeModifiersForNewBlockIndex(CBlockIndex* pindexNew);

#endif // BITCOIN_KERNEL_H
