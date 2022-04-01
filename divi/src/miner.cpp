// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"

#include "amount.h"
#include "chainparams.h"
#include <ChainstateManager.h>
#include <masternode-payments.h>
#include <MasternodeModule.h>
#include <CoinMintingModule.h>
#include <I_CoinMinter.h>
#include <Logging.h>
#include <spork.h>
#include <Settings.h>
#include <coins.h>
#include <ThreadManagementHelpers.h>
#include <net.h>
#include <chain.h>
#include <map>
#include <FeeAndPriorityCalculator.h>

#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>

extern Settings& settings;
extern CCriticalSection cs_main;
extern CTxMemPool mempool;

typedef std::map<unsigned int, unsigned int> LastExtensionTimestampByBlockHeight;
LastExtensionTimestampByBlockHeight& getLastExtensionTimestampByBlockHeight()
{
    static std::map<unsigned int, unsigned int> mapHashedBlocks;
    return mapHashedBlocks;
}

std::unique_ptr<CoinMintingModule> coinMintingModule(nullptr);
void InitializeCoinMintingModule(I_StakingWallet* pwallet)
{
    static const CSporkManager& sporkManager = GetSporkManager();
    static const CChainParams& chainParameters = Params();
    static const MasternodeModule& masternodeModule = GetMasternodeModule();
    static const I_PeerBlockNotifyService& peerNotification = GetPeerBlockNotifyService();
    static const auto& chainstate = ChainstateManager::Get();
    coinMintingModule.reset(
        new CoinMintingModule(
            settings,
            cs_main,
            chainParameters,
            chainstate,
            masternodeModule,
            FeeAndPriorityCalculator::instance().getMinimumRelayFeeRate(),
            mempool,
            peerNotification,
            *pwallet,
            getLastExtensionTimestampByBlockHeight(),
            sporkManager));

}
const CoinMintingModule& GetCoinMintingModule()
{
    assert(static_cast<bool>(coinMintingModule));
    return *coinMintingModule;
}
//////////////////////////////////////////////////////////////////////////////
//
// DIVIMiner
//

#ifdef ENABLE_WALLET
//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//
void MintCoins(
    I_CoinMinter& minter)
{
    while (minter.mintingHasBeenRequested())
    {
        if (!minter.CanMintCoins())
        {
            minter.sleep(5000);
        }
        else
        {
            minter.createNewBlock();
        }
    }

}
void MinterThread(I_CoinMinter& minter)
{
    LogPrintf("DIVIMiner started\n");
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("divi-miner");

    // Each thread has its own key and counter

    while(true) {

        try {
            MintCoins(minter);
        }
        catch (const boost::thread_interrupted&)
        {
            LogPrintf("MinterThread -- terminated\n");
            throw;
        }
        catch (const std::runtime_error &e)
        {
            LogPrintf("MinterThread -- runtime error: %s\n", e.what());
            return;
        }
    }
}

bool CheckHeightForRecentProofOfStakeGeneration(const int blockHeight)
{
    static const LastExtensionTimestampByBlockHeight& mapHashedBlocks = getLastExtensionTimestampByBlockHeight();
    constexpr int64_t fiveMinutes = 5*60;
    const auto it = mapHashedBlocks.find(blockHeight);
    return it != mapHashedBlocks.end() && GetTime() - it->second < fiveMinutes;
}

bool HasRecentlyAttemptedToGenerateProofOfStake()
{
    const ChainstateManager::Reference chainstate;
    const int currentChainHeight = chainstate->ActiveChain().Tip()->nHeight;
    return CheckHeightForRecentProofOfStakeGeneration(currentChainHeight) ||
        CheckHeightForRecentProofOfStakeGeneration(currentChainHeight - 1);
}

// ppcoin: stake minter thread
void ThreadCoinMinter()
{
    boost::this_thread::interruption_point();
    LogPrintf("ThreadCoinMinter started\n");
    try {
        const CoinMintingModule& mintingModule = GetCoinMintingModule();
        I_CoinMinter& minter = mintingModule.coinMinter();
        minter.setMintingRequestStatus(true);
        MinterThread(minter);
        boost::this_thread::interruption_point();
    } catch (std::exception& e) {
        LogPrintf("ThreadCoinMinter() exception \n");
    } catch (...) {
        LogPrintf("ThreadCoinMinter() error \n");
    }
    LogPrintf("ThreadCoinMinter exiting,\n");
}
#endif // ENABLE_WALLET
