// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"

#include "amount.h"
#include "chainparams.h"
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
extern CCoinsViewCache* pcoinsTip;
extern CChain chainActive;
extern CCriticalSection cs_main;
extern CTxMemPool mempool;
extern BlockMap mapBlockIndex;

LastExtensionTimestampByBlockHeight& getLastExtensionTimestampByBlockHeight()
{
    static std::map<unsigned int, unsigned int> mapHashedBlocks;
    return mapHashedBlocks;
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

bool HasRecentlyAttemptedToGenerateProofOfStake()
{
    static const LastExtensionTimestampByBlockHeight& mapHashedBlocks = getLastExtensionTimestampByBlockHeight();
    bool recentlyAttemptedPoS = false;
    if (mapHashedBlocks.count(chainActive.Tip()->nHeight))
        recentlyAttemptedPoS = true;
    else if (mapHashedBlocks.count(chainActive.Tip()->nHeight - 1))
        recentlyAttemptedPoS = true;

    return recentlyAttemptedPoS;
}

// ppcoin: stake minter thread
void ThreadStakeMinter(I_StakingWallet* pwallet)
{
    static const CSporkManager& sporkManager = GetSporkManager();
    static LastExtensionTimestampByBlockHeight& mapHashedBlocks = getLastExtensionTimestampByBlockHeight();
    boost::this_thread::interruption_point();
    LogPrintf("ThreadStakeMinter started\n");
    try {
        static CoinMintingModule mintingModule(
            settings,
            cs_main,Params(),
            chainActive,
            mapBlockIndex,
            GetMasternodeModule(),
            FeeAndPriorityCalculator::instance().getMinimumRelayFeeRate(),
            pcoinsTip,
            mempool,
            GetPeerBlockNotifyService(),
            *pwallet,
            mapHashedBlocks,
            sporkManager);
        static I_CoinMinter& minter = mintingModule.coinMinter();
        minter.setMintingRequestStatus(true);
        MinterThread(minter);
        boost::this_thread::interruption_point();
    } catch (std::exception& e) {
        LogPrintf("ThreadStakeMinter() exception \n");
    } catch (...) {
        LogPrintf("ThreadStakeMinter() error \n");
    }
    LogPrintf("ThreadStakeMinter exiting,\n");
}

void static ThreadPoWMinter(I_StakingWallet* pwallet)
{
    static const CSporkManager& sporkManager = GetSporkManager();
    static LastExtensionTimestampByBlockHeight& mapHashedBlocks = getLastExtensionTimestampByBlockHeight();
    boost::this_thread::interruption_point();
    try {
        static CoinMintingModule mintingModule(
            settings,
            cs_main,
            Params(),
            chainActive,
            mapBlockIndex,
            GetMasternodeModule(),
            FeeAndPriorityCalculator::instance().getMinimumRelayFeeRate(),
            pcoinsTip,
            mempool,
            GetPeerBlockNotifyService(),
            *pwallet,
            mapHashedBlocks,
            sporkManager);
        static I_CoinMinter& minter = mintingModule.coinMinter();
        minter.setMintingRequestStatus(true);
        MinterThread(minter);
        boost::this_thread::interruption_point();
    } catch (std::exception& e) {
        LogPrintf("ThreadPoWMinter() exception: %s\n");
    } catch (...) {
        LogPrintf("ThreadPoWMinter() unknown exception");
    }

    LogPrintf("ThreadPoWMinter exiting\n");
}

void SetPoWThreadPool(I_StakingWallet* pwallet, int nThreads)
{
    static boost::thread_group* minerThreads = NULL;

    if (nThreads < 0) {
        // In regtest threads defaults to 1
        if (Params().DefaultMinerThreads())
            nThreads = Params().DefaultMinerThreads();
        else
            nThreads = boost::thread::hardware_concurrency();
    }

    if (minerThreads != NULL) {
        minerThreads->interrupt_all();
        delete minerThreads;
        minerThreads = NULL;
    }

    if (nThreads == 0 || pwallet == nullptr)
        return;

    minerThreads = new boost::thread_group();
    for (int i = 0; i < nThreads; i++)
        minerThreads->create_thread(boost::bind(&ThreadPoWMinter, pwallet));
}

#endif // ENABLE_WALLET
