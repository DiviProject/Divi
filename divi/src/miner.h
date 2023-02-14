// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MINER_H
#define BITCOIN_MINER_H

#include <stdint.h>
#include <map>
#include <atomic>

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
class CMasternodeSync;
class CSporkManager;
class I_DifficultyAdjuster;
class I_BlockProofProver;

namespace boost
{
class thread_group;
} // namespace boost

struct CoinMintingModuleReference
{
private:
    const CoinMintingModule* const coinMintingModuleRef_;
    std::atomic<int>& referenceCounter_;
public:
    CoinMintingModuleReference(
        const CoinMintingModule& coinMintingModule,
        std::atomic<int>& referenceCounter
        ): coinMintingModuleRef_(&coinMintingModule)
        , referenceCounter_(referenceCounter)
    {
        ++referenceCounter_;
    }
    ~CoinMintingModuleReference()
    {
        --referenceCounter_;
    }

    const CoinMintingModule& access() const
    {
        return *coinMintingModuleRef_;
    }
};

void InitializeCoinMintingModule(
    const Settings& settings,
    const CChainParams& chainParameters,
    const I_BlockProofProver& blockProofProver,
    const CMasternodeSync& masternodeSynchronization,
    const CFeeRate& minimumRelayFeeRate,
    const I_PeerBlockNotifyService& peerNotificationService,
    const I_BlockSubmitter& blockSubmitter,
    const I_DifficultyAdjuster& difficultyAdjuster,
    std::map<unsigned int, unsigned int>& mapHashedBlocks,
    CCriticalSection& mainCS,
    CTxMemPool& mempool,
    I_StakingWallet& stakingWallet,
    boost::thread_group& backgroundThreadGroup);
void ShutdownCoinMintingModule();
const CoinMintingModuleReference GetCoinMintingModule();

/** Run the miner threads */
void ThreadCoinMinter();

void MintCoins(I_CoinMinter& minter);
#endif // BITCOIN_MINER_H
