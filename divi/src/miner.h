// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MINER_H
#define BITCOIN_MINER_H

#include <stdint.h>

class CBlock;
class CBlockHeader;
class CBlockIndex;
class CReserveKey;
class CScript;
class CWallet;
class I_CoinMinter;

struct CBlockTemplate;

/** Run the miner threads */
void GenerateDivi(bool fGenerate, CWallet* pwallet, int nThreads);
void ThreadStakeMinter(CWallet* pwallet);
/** Generate a new block, without valid proof-of-work */
CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn, CWallet* pwallet, bool fProofOfStake);
CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, CWallet* pwallet, bool fProofOfStake);
/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce);
/** Check mined block */
void UpdateTime(CBlockHeader* block, const CBlockIndex* pindexPrev);

void MintCoins(
    bool& fMintableCoins, 
    bool fProofOfStake, 
    I_CoinMinter& minter,
    unsigned int nExtraNonce,
    CReserveKey& reservekey);
void MinterThread(CWallet* pwallet, bool fProofOfStake, I_CoinMinter& minter);

#endif // BITCOIN_MINER_H
