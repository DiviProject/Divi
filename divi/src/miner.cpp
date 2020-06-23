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
bool fGenerateBitcoins = false;

// ***TODO*** that part changed in bitcoin, we are using a mix with old one here for now

void BitcoinMiner(CWallet* pwallet, bool fProofOfStake, I_CoinMinter& minter)
{
    LogPrintf("DIVIMiner started\n");
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("divi-miner");

    // Each thread has its own key and counter
    CReserveKey reservekey(pwallet);
    unsigned int nExtraNonce = 0;

    //control the amount of times the client will check for mintable coins
    static bool fMintableCoins = false;

    while(true) {

        try {

            if (fProofOfStake) // 5 minute check time
            {
                fMintableCoins = minter.isMintable();
            }

            while (fGenerateBitcoins || fProofOfStake) 
            {
                if (fProofOfStake) 
                {
                    if (!fMintableCoins ||
                        !minter.isAtProofOfStakeHeight() ||
                        !minter.satisfiesMintingRequirements() ||
                        minter.limitStakingSpeed())
                    {
                        minter.sleep(5000);
                        continue;
                    }
                }

                //
                // Create new block
                //
                if(!minter.createNewBlock(nExtraNonce,reservekey,fProofOfStake))
                {
                    continue;
                }
            }
        }
        catch (const boost::thread_interrupted&)
        {
            LogPrintf("BitcoinMiner -- terminated\n");
            throw;
        }
        catch (const std::runtime_error &e)
        {
            LogPrintf("BitcoinMiner -- runtime error: %s\n", e.what());
            return;
        }
    }
}

// ppcoin: stake minter thread
void ThreadStakeMinter(CWallet* pwallet)
{
    boost::this_thread::interruption_point();
    LogPrintf("ThreadStakeMinter started\n");
    try {
        CoinMinter minter(pwallet, chainActive, Params(),vNodes,masternodeSync,mapHashedBlocks);
        BitcoinMiner(pwallet, true,minter);
        boost::this_thread::interruption_point();
    } catch (std::exception& e) {
        LogPrintf("ThreadStakeMinter() exception \n");
    } catch (...) {
        LogPrintf("ThreadStakeMinter() error \n");
    }
    LogPrintf("ThreadStakeMinter exiting,\n");
}

void static ThreadBitcoinMiner(void* parg)
{
    boost::this_thread::interruption_point();
    CWallet* pwallet = (CWallet*)parg;

    try {
        CoinMinter minter(pwallet, chainActive, Params(),vNodes,masternodeSync,mapHashedBlocks);
        BitcoinMiner(pwallet, false, minter);
        boost::this_thread::interruption_point();
    } catch (std::exception& e) {
        LogPrintf("ThreadBitcoinMiner() exception: %s\n");
    } catch (...) {
        LogPrintf("ThreadBitcoinMiner() unknown exception");
    }

    LogPrintf("ThreadBitcoinMiner exiting\n");
}

void GenerateBitcoins(bool fGenerate, CWallet* pwallet, int nThreads)
{
    static boost::thread_group* minerThreads = NULL;
    fGenerateBitcoins = fGenerate;

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
        minerThreads->create_thread(boost::bind(&ThreadBitcoinMiner, pwallet));
}

#endif // ENABLE_WALLET
