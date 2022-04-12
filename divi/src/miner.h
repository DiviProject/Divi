// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MINER_H
#define BITCOIN_MINER_H

#include <stdint.h>
#include <map>

class I_StakingWallet;
class I_CoinMinter;
class CoinMintingModule;
class I_PeerBlockNotifyService;

void InitializeCoinMintingModule(const I_PeerBlockNotifyService& peerNotificationService, I_StakingWallet* pwallet);
void DestructCoinMintingModule();
const CoinMintingModule& GetCoinMintingModule();

/** Run the miner threads */
void ThreadCoinMinter();

void MintCoins(I_CoinMinter& minter);
bool HasRecentlyAttemptedToGenerateProofOfStake();


#endif // BITCOIN_MINER_H
