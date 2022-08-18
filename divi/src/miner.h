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
class CTxMemPool;
class CCriticalSection;
class Settings;
class CFeeRate;
class I_BlockSubmitter;
class CChainParams;
class MasternodeModule;
class CSporkManager;
namespace boost
{
class thread_group;
} // namespace boost


void InitializeCoinMintingModule(
    const Settings& settings,
    const CChainParams& chainParameters,
    const MasternodeModule& masternodeModule,
    const CSporkManager& sporkManager,
    const CFeeRate& minimumRelayFeeRate,
    const I_PeerBlockNotifyService& peerNotificationService,
    const I_BlockSubmitter& blockSubmitter,
    CCriticalSection& mainCS,
    CTxMemPool& mempool,
    I_StakingWallet& stakingWallet,
    boost::thread_group& backgroundThreadGroup);
void ShutdownCoinMintingModule();
const CoinMintingModule& GetCoinMintingModule();

/** Run the miner threads */
void ThreadCoinMinter();

void MintCoins(I_CoinMinter& minter);
bool HasRecentlyAttemptedToGenerateProofOfStake();


#endif // BITCOIN_MINER_H
