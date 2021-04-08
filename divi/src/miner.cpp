// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"

#include "amount.h"
#include "main.h"
#include <masternode-payments.h>
#include <MasternodeModule.h>
#include <CoinMintingModule.h>
#include <I_CoinMinter.h>
#include <Logging.h>
#include <spork.h>
#include <Settings.h>
#include <coins.h>
#include <FeeRate.h>
#include <ThreadManagementHelpers.h>

#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>

extern Settings& settings;
extern CCoinsViewCache* pcoinsTip;
extern CFeeRate minRelayTxFee;
//////////////////////////////////////////////////////////////////////////////
//
// DIVIMiner
//

#ifdef ENABLE_WALLET
//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//
extern CWallet* pwalletMain;
bool fGenerateDivi = false;

void MintCoins(
    bool fProofOfStake,
    I_CoinMinter& minter,
    unsigned int nExtraNonce)
{
    while (minter.mintingHasBeenRequested())
    {
        if (fProofOfStake && !minter.CanMintCoins())
        {
            minter.sleep(5000);
        }
        else
        {
            minter.createNewBlock(nExtraNonce,fProofOfStake);
        }
    }

}
void MinterThread(bool fProofOfStake, I_CoinMinter& minter)
{
    LogPrintf("DIVIMiner started\n");
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("divi-miner");

    // Each thread has its own key and counter
    unsigned int nExtraNonce = 0;

    while(true) {

        try {
            MintCoins(fProofOfStake,minter,nExtraNonce);
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
    bool recentlyAttemptedPoS = false;
    if (mapHashedBlocks.count(chainActive.Tip()->nHeight))
        recentlyAttemptedPoS = true;
    else if (mapHashedBlocks.count(chainActive.Tip()->nHeight - 1))
        recentlyAttemptedPoS = true;

    return recentlyAttemptedPoS;
}

// ppcoin: stake minter thread
void ThreadStakeMinter(CWallet* pwallet)
{
    boost::this_thread::interruption_point();
    LogPrintf("ThreadStakeMinter started\n");
    try {
        static CoinMintingModule mintingModule(
            settings,
            cs_main,Params(),
            chainActive,
            GetMasternodeSync(),
            minRelayTxFee,
            pcoinsTip,
            GetMasternodePayments(),
            mempool,
            vNodes,
            *pwallet,
            mapHashedBlocks,
            mapBlockIndex,
            sporkManager);
        static I_CoinMinter& minter = mintingModule.coinMinter();
        bool isProofOfStake = true;
        minter.setMintingRequestStatus(isProofOfStake);
        MinterThread(isProofOfStake,minter);
        boost::this_thread::interruption_point();
    } catch (std::exception& e) {
        LogPrintf("ThreadStakeMinter() exception \n");
    } catch (...) {
        LogPrintf("ThreadStakeMinter() error \n");
    }
    LogPrintf("ThreadStakeMinter exiting,\n");
}

void static ThreadPoWMinter(void* parg)
{
    boost::this_thread::interruption_point();
    CWallet* pwallet = (CWallet*)parg;

    try {
        static CoinMintingModule mintingModule(
            settings,
            cs_main,
            Params(),
            chainActive,
            GetMasternodeSync(),
            minRelayTxFee,
            pcoinsTip,
            GetMasternodePayments(),
            mempool,
            vNodes,
            *pwallet,
            mapHashedBlocks,
            mapBlockIndex,
            sporkManager);
        static I_CoinMinter& minter = mintingModule.coinMinter();
        bool isProofOfStake = false;
        minter.setMintingRequestStatus(fGenerateDivi);
        MinterThread(isProofOfStake, minter);
        boost::this_thread::interruption_point();
    } catch (std::exception& e) {
        LogPrintf("ThreadPoWMinter() exception: %s\n");
    } catch (...) {
        LogPrintf("ThreadPoWMinter() unknown exception");
    }

    LogPrintf("ThreadPoWMinter exiting\n");
}

void GenerateDivi(bool fGenerate, CWallet* pwallet, int nThreads)
{
    static boost::thread_group* minerThreads = NULL;
    fGenerateDivi = fGenerate;

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

    if (nThreads == 0 || !fGenerate)
        return;

    minerThreads = new boost::thread_group();
    for (int i = 0; i < nThreads; i++)
        minerThreads->create_thread(boost::bind(&ThreadPoWMinter, pwallet));
}

#endif // ENABLE_WALLET
