// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"

#include <ChainstateManager.h>
#include <CoinMintingModule.h>
#include <I_CoinMinter.h>
#include <Logging.h>
#include <ThreadManagementHelpers.h>
#include <chain.h>
#include <sync.h>
#include <Settings.h>

#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>

namespace
{

 template <typename T>
 struct NonDeletionDeleter {
  void operator()(T* r) {}
 };

CCriticalSection cs_coinMintingModule;
std::unique_ptr<CoinMintingModule> coinMintingModule;
std::unique_ptr<boost::thread, NonDeletionDeleter<boost::thread>> backgroundMintingThread(nullptr);
volatile bool moduleInitialized = false;

void InterruptMintingThread()
{
    if(backgroundMintingThread)
    {
        try
        {
            backgroundMintingThread->interrupt();
        }
        catch(boost::thread_interrupted)
        {
        }
        backgroundMintingThread.release();
    }
}

void StopMinting()
{
    if(coinMintingModule)
    {
        coinMintingModule->coinMinter().setMintingRequestStatus(false);
    }
}
void DestructCoinMintingModule()
{
    LOCK(cs_coinMintingModule);
    assert(!moduleInitialized || coinMintingModule != nullptr);
    coinMintingModule.reset();
}
} // anonymous namespace

void InitializeCoinMintingModule(
    const Settings& settings,
    const CChainParams& chainParameters,
    const MasternodeModule& masternodeModule,
    const SuperblockSubsidyContainer& blockSubsidies,
    const BlockIncentivesPopulator& incentives,
    const CSporkManager& sporkManager,
    const I_ProofOfStakeGenerator& proofGenerator,
    const CFeeRate& minimumRelayFeeRate,
    const I_PeerBlockNotifyService& peerNotificationService,
    const I_BlockSubmitter& blockSubmitter,
    std::map<unsigned int, unsigned int>& mapHashedBlocks,
    CCriticalSection& mainCS,
    CTxMemPool& mempool,
    I_StakingWallet& stakingWallet,
    boost::thread_group& backgroundThreadGroup)
{
    LOCK(cs_coinMintingModule);
    assert(coinMintingModule == nullptr);
    coinMintingModule.reset(
        new CoinMintingModule(
            settings,
            chainParameters,
            masternodeModule,
            blockSubsidies,
            incentives,
            proofGenerator,
            minimumRelayFeeRate,
            peerNotificationService,
            blockSubmitter,
            sporkManager,
            mapHashedBlocks,
            mainCS,
            mempool,
            stakingWallet));
    moduleInitialized = true;
    if (settings.GetBoolArg("-staking", true))
    {
        backgroundMintingThread.reset(
            backgroundThreadGroup.create_thread(
                boost::bind(
                    &TraceThread<void (*)()>,
                    "coinmint",
                    &ThreadCoinMinter))
        );
    }
}

void ShutdownCoinMintingModule()
{
    StopMinting();
    InterruptMintingThread();
    DestructCoinMintingModule();
}

const CoinMintingModule& GetCoinMintingModule()
{
    assert(coinMintingModule != nullptr);
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
        if (!minter.canMintCoins())
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
    LogPrintf("%s started\n",__func__);
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("divi-miner");

    // Each thread has its own key and counter
    try {
        MintCoins(minter);
    }
    catch (const boost::thread_interrupted&)
    {
        LogPrintf("%s -- terminated\n",__func__);
        throw;
    }
    catch (const std::runtime_error &e)
    {
        LogPrintf("%s -- runtime error: %s\n", __func__, e.what());
        return;
    }
}

// ppcoin: stake minter thread
void ThreadCoinMinter()
{
    boost::this_thread::interruption_point();
    LogPrintf("%s started\n",__func__);
    try {
        /* While the thread is running, we keep the mutex locked; this ensures
           that the module will not be reset or destructed while in use.  */
        LOCK(cs_coinMintingModule);
        const CoinMintingModule& mintingModule = GetCoinMintingModule();
        I_CoinMinter& minter = mintingModule.coinMinter();
        minter.setMintingRequestStatus(true);
        MinterThread(minter);
        boost::this_thread::interruption_point();
    } catch (std::exception& e) {
        LogPrintf("%s exception \n",__func__);
    } catch (...) {
        LogPrintf("%s error \n",__func__);
    }
    LogPrintf("%s exiting,\n",__func__);
}
#endif // ENABLE_WALLET
