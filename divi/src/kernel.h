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
class CBlockRewards;
class CBlock;
class CCoinsViewCache;
class CTransaction;
class COutPoint;
class BlockMap;
class CChain;
struct StakingData;

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(
    CChain& activeChain,
    const CBlock& block,
    CBlockIndex* pindexPrev,
    uint256& hashProofOfStake);

/** Checks if the transaction is a valid coinstake after the staking vault
 *  fork (which adds extra rules, like paying back at least the expected
 *  staking reward to the same script that the staking input came from).
 *  Note that the extra conditions only apply to actual stake inputs that
 *  are vault scripts; if the tx is a coinstake but the input is not a vault,
 *  then the fucntion just returns true without further checks.  */
bool CheckCoinstakeForVaults(const CTransaction& tx,
                             const CBlockRewards& expectedRewards,
                             const CCoinsViewCache& view);

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(
    int nHeight,
    unsigned int nStakeModifierChecksum);

void SetStakeModifiersForNewBlockIndex(CBlockIndex* pindexNew);

#endif // BITCOIN_KERNEL_H
