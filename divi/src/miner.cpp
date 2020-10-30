// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"

#include "amount.h"
#include "main.h"
#include "masternode-sync.h"
#ifdef ENABLE_WALLET
#include "wallet.h"
#endif
#include <BlockFactory.h>
#include "CoinMinter.h"

#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>

//////////////////////////////////////////////////////////////////////////////
//
// DIVIMiner
//

#ifdef ENABLE_WALLET
//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//
bool fGenerateDivi = false;

// ***TODO*** that part changed in bitcoin, we are using a mix with old one here for now
void MintCoins(
    bool fProofOfStake,
    I_CoinMinter& minter,
    unsigned int nExtraNonce,
    CReserveKey& reservekey)
{
    while (minter.mintingHasBeenRequested())
    {
        if (fProofOfStake && !minter.CanMintCoins())
        {
            minter.sleep(5000);
        }
        else
        {
            minter.createNewBlock(nExtraNonce,reservekey,fProofOfStake);
        }
    }

}
void MinterThread(CWallet* pwallet, bool fProofOfStake, I_CoinMinter& minter)
{
    LogPrintf("DIVIMiner started\n");
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("divi-miner");

    // Each thread has its own key and counter
    CReserveKey reservekey(pwallet);
    unsigned int nExtraNonce = 0;

    while(true) {

        try {
            MintCoins(fProofOfStake,minter,nExtraNonce, reservekey);
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

int64_t nLastCoinStakeSearchInterval = 0;
bool HasRecentlyAttemptedToGenerateProofOfStake()
{
    bool recentlyAttemptedPoS = false;
    if (mapHashedBlocks.count(chainActive.Tip()->nHeight))
        recentlyAttemptedPoS = true;
    else if (mapHashedBlocks.count(chainActive.Tip()->nHeight - 1) && nLastCoinStakeSearchInterval)
        recentlyAttemptedPoS = true;

    return recentlyAttemptedPoS;
}

// ppcoin: stake minter thread
void ThreadStakeMinter(CWallet* pwallet)
{
    boost::this_thread::interruption_point();
    LogPrintf("ThreadStakeMinter started\n");
    try {
        static const CChainParams& chainParameters = Params();
        static BlockFactory blockFactory(*pwallet,nLastCoinStakeSearchInterval,mapHashedBlocks,chainActive,chainParameters, mempool,cs_main);
        static CoinMinter minter(blockFactory, pwallet, chainActive, chainParameters,vNodes,masternodeSync,mapHashedBlocks,mempool,cs_main,nLastCoinStakeSearchInterval);
        bool isProofOfStake = true;
        minter.setMintingRequestStatus(isProofOfStake);
        MinterThread(pwallet, isProofOfStake,minter);
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
        static const CChainParams& chainParameters = Params();
        static BlockFactory blockFactory(*pwallet,nLastCoinStakeSearchInterval,mapHashedBlocks,chainActive,chainParameters, mempool,cs_main);
        static CoinMinter minter(blockFactory,pwallet, chainActive, chainParameters,vNodes,masternodeSync,mapHashedBlocks,mempool,cs_main,nLastCoinStakeSearchInterval);
        bool isProofOfStake = false;
        minter.setMintingRequestStatus(fGenerateDivi);
        MinterThread(pwallet, isProofOfStake, minter);
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
