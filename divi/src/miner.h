// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MINER_H
#define BITCOIN_MINER_H

#include <stdint.h>
#include <map>

class CBlock;
class CBlockHeader;
class CBlockIndex;
class CScript;
class I_StakingWallet;
class I_CoinMinter;

struct CBlockTemplate;

/** Run the miner threads */
void SetPoWThreadPool(I_StakingWallet* pwallet, int nThreads = -1);
void ThreadStakeMinter(I_StakingWallet* pwallet);

void MintCoins(
    bool fProofOfStake,
    I_CoinMinter& minter);
void MinterThread(bool fProofOfStake, I_CoinMinter& minter);
bool HasRecentlyAttemptedToGenerateProofOfStake();

typedef std::map<unsigned int, unsigned int> LastExtensionTimestampByBlockHeight;
LastExtensionTimestampByBlockHeight& getLastExtensionTimestampByBlockHeight();

#endif // BITCOIN_MINER_H
