// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"

#include "amount.h"
#include "hash.h"
#include "main.h"
#include "masternode-sync.h"
#include "net.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#ifdef ENABLE_WALLET
#include "wallet.h"
#endif
#include "masternode-payments.h"
#include "spork.h"
#include "SuperblockHelpers.h"
#include "CoinMinter.h"
#include "BlockMemoryPoolTransactionCollector.h"

#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>


#include "FeeAndPriorityCalculator.h"

using namespace std;

//////////////////////////////////////////////////////////////////////////////
//
// DIVIMiner
//
extern int64_t nLastCoinStakeSearchInterval;

bool CreateAndFindStake(
    int64_t nSearchTime, 
    int64_t& nLastCoinStakeSearchTime, 
    CWallet& pwallet, 
    CBlock& block, 
    CMutableTransaction& txCoinStake)
{

    bool fStakeFound = false;
    if (nSearchTime >= nLastCoinStakeSearchTime) {
        unsigned int nTxNewTime = 0;
        if (pwallet.CreateCoinStake(pwallet, block.nBits, nSearchTime - nLastCoinStakeSearchTime, txCoinStake, nTxNewTime)) {
            block.nTime = nTxNewTime;
            block.vtx[0].vout[0].SetEmpty();
            block.vtx.push_back(CTransaction(txCoinStake));
            fStakeFound = true;
        }
        nLastCoinStakeSearchInterval = nSearchTime - nLastCoinStakeSearchTime;
        nLastCoinStakeSearchTime = nSearchTime;
    }
    return fStakeFound;
}

void SetRequiredWork(CBlock& block)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    block.nBits = GetNextWorkRequired(pindexPrev, &block,Params());
}

int64_t SetBlockTime(CBlock& block)
{
    block.nTime = GetAdjustedTime();
    int64_t nSearchTime = block.nTime; // search to current time
    return nSearchTime;
}

void AddTransactionToBlockAndSetDefaultFees(CBlock& block, std::unique_ptr<CBlockTemplate>& pblocktemplate, const CMutableTransaction& txNew)
{
    block.vtx.push_back(txNew);
    pblocktemplate->vTxFees.push_back(-1);   // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end
}

void UpdateTime(CBlockHeader* block, const CBlockIndex* pindexPrev)
{
    block->nTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());

    // Updating time can change work required on testnet:
    if (Params().AllowMinDifficultyBlocks())
        block->nBits = GetNextWorkRequired(pindexPrev, block,Params());
}

CMutableTransaction CreateCoinbaseTransaction(const CScript& scriptPubKeyIn)
{
    CMutableTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKeyIn;
    return txNew;
}

CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn, CWallet* pwallet, bool fProofOfStake)
{
    CReserveKey reservekey(pwallet);

    // Create new block
    unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    if (!pblocktemplate.get())
        return NULL;
    CBlock* block = &pblocktemplate->block; // pointer for convenience

    // Create coinbase tx
    CMutableTransaction txNew = CreateCoinbaseTransaction(scriptPubKeyIn);
    
    AddTransactionToBlockAndSetDefaultFees(*block, pblocktemplate, txNew);

    // ppcoin: if coinstake available add coinstake tx
    static int64_t nLastCoinStakeSearchTime = GetAdjustedTime(); // only initialized at startup

    if (fProofOfStake) {
        boost::this_thread::interruption_point();
        
        CMutableTransaction txCoinStake;

        SetRequiredWork(*block);

        if (!CreateAndFindStake(SetBlockTime(*block), nLastCoinStakeSearchTime, *pwallet, *block, txCoinStake))
            return NULL;
    }

    // Collect memory pool transactions into the block

    if(!BlockMemoryPoolTransactionCollector (mempool,cs_main)
        .CollectTransactionsIntoBlock(
            pblocktemplate,
            fProofOfStake,
            txNew
        ))
    {
        return NULL;
    }

    LogPrintf("CreateNewBlock(): releasing template %s\n", "");
    return pblocktemplate.release();
}

void IncrementExtraNonce(CBlock* block, CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != block->hashPrevBlock) {
        nExtraNonce = 0;
        hashPrevBlock = block->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight + 1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(block->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    block->vtx[0] = txCoinbase;
    block->hashMerkleRoot = block->BuildMerkleTree();
}

#ifdef ENABLE_WALLET
//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//
double dHashesPerSec = 0.0;
int64_t nHPSTimerStart = 0;

CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, CWallet* pwallet, bool fProofOfStake)
{
    CPubKey pubkey;
    if (!reservekey.GetReservedKey(pubkey, false))
        return NULL;

    CScript scriptPubKey = CScript() << ToByteVector(pubkey) << OP_CHECKSIG;
    return CreateNewBlock(scriptPubKey, pwallet, fProofOfStake);
}

bool ProcessBlockFound(CBlock* block, CWallet& wallet, CReserveKey& reservekey)
{
    LogPrintf("%s\n", block->ToString());
    LogPrintf("generated %s\n", FormatMoney(block->vtx[0].vout[0].nValue));

    // Found a solution
    {
        LOCK(cs_main);
        if (block->hashPrevBlock != chainActive.Tip()->GetBlockHash())
            return error("DIVIMiner : generated block is stale");
    }

    // Remove key from key pool
    reservekey.KeepKey();

    // Track how many getdata requests this block gets
    {
        LOCK(wallet.cs_wallet);
        wallet.mapRequestCount[block->GetHash()] = 0;
    }

    // Process this block the same as if we had received it from another node
    CValidationState state;
    if (!ProcessNewBlock(state, NULL, block))
        return error("DIVIMiner : ProcessNewBlock, block not accepted");

    for (CNode* node : vNodes) {
        node->PushInventory(CInv(MSG_BLOCK, block->GetHash()));
    }

    return true;
}

bool fGenerateBitcoins = false;

// ***TODO*** that part changed in bitcoin, we are using a mix with old one here for now

void BitcoinMiner(CWallet* pwallet, bool fProofOfStake)
{
    LogPrintf("DIVIMiner started\n");
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("divi-miner");

    // Each thread has its own key and counter
    CReserveKey reservekey(pwallet);
    unsigned int nExtraNonce = 0;

    //control the amount of times the client will check for mintable coins
    static bool fMintableCoins = false;
    static CoinMinter minter(pwallet, chainActive, Params(),vNodes,masternodeSync,mapHashedBlocks);

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

                unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
                CBlockIndex* pindexPrev = chainActive.Tip();
                if (!pindexPrev)
                    continue;

                unique_ptr<CBlockTemplate> pblocktemplate(CreateNewBlockWithKey(reservekey, pwallet, fProofOfStake));

                if (!pblocktemplate.get())
                    continue;

                CBlock* block = &pblocktemplate->block;
                IncrementExtraNonce(block, pindexPrev, nExtraNonce);

                //Stake miner main
                if (fProofOfStake) {
                    LogPrintf("CPUMiner : proof-of-stake block found %s \n", block->GetHash().ToString().c_str());

                    if (!block->SignBlock(*pwallet)) {
                        LogPrintf("BitcoinMiner(): Signing new block failed \n");
                        continue;
                    }

                    LogPrintf("CPUMiner : proof-of-stake block was signed %s \n", block->GetHash().ToString().c_str());
                    SetThreadPriority(THREAD_PRIORITY_NORMAL);
                    ProcessBlockFound(block, *pwallet, reservekey);
                    SetThreadPriority(THREAD_PRIORITY_LOWEST);

                    continue;
                }

                LogPrintf("Running DIVIMiner with %u transactions in block (%u bytes)\n", block->vtx.size(),
                          ::GetSerializeSize(*block, SER_NETWORK, PROTOCOL_VERSION));

                //
                // Search
                //
                int64_t nStart = GetTime();
                uint256 hashTarget = uint256().SetCompact(block->nBits);
                while (true) {
                    unsigned int nHashesDone = 0;

                    uint256 hash;
                    while (true) {
                        hash = block->GetHash();
                        if (hash <= hashTarget) {
                            // Found a solution
                            SetThreadPriority(THREAD_PRIORITY_NORMAL);
                            LogPrintf("BitcoinMiner:\n");
                            LogPrintf("proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash.GetHex(), hashTarget.GetHex());
                            ProcessBlockFound(block, *pwallet, reservekey);
                            SetThreadPriority(THREAD_PRIORITY_LOWEST);

                            // In regression test mode, stop mining after a block is found. This
                            // allows developers to controllably generate a block on demand.
                            if (Params().MineBlocksOnDemand())
                                throw boost::thread_interrupted();

                            break;
                        }
                        block->nNonce += 1;
                        nHashesDone += 1;
                        if ((block->nNonce & 0xFF) == 0)
                            break;
                    }

                    // Meter hashes/sec
                    static int64_t nHashCounter;
                    if (nHPSTimerStart == 0) {
                        nHPSTimerStart = GetTimeMillis();
                        nHashCounter = 0;
                    } else
                        nHashCounter += nHashesDone;
                    if (GetTimeMillis() - nHPSTimerStart > 4000) {
                        static CCriticalSection cs;
                        {
                            LOCK(cs);
                            if (GetTimeMillis() - nHPSTimerStart > 4000) {
                                dHashesPerSec = 1000.0 * nHashCounter / (GetTimeMillis() - nHPSTimerStart);
                                nHPSTimerStart = GetTimeMillis();
                                nHashCounter = 0;
                                static int64_t nLogTime;
                                if (GetTime() - nLogTime > 30 * 60) {
                                    nLogTime = GetTime();
                                    LogPrintf("hashmeter %6.0f khash/s\n", dHashesPerSec / 1000.0);
                                }
                            }
                        }
                    }

                    // Check for stop or if block needs to be rebuilt
                    boost::this_thread::interruption_point();
                    // Regtest mode doesn't require peers
                    if (vNodes.empty() && Params().MiningRequiresPeers())
                        break;
                    if (block->nNonce >= 0xffff0000)
                        break;
                    if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60)
                        break;
                    if (pindexPrev != chainActive.Tip())
                        break;

                    // Update nTime every few seconds
                    UpdateTime(block, pindexPrev);
                    if (Params().AllowMinDifficultyBlocks()) {
                        // Changing block->nTime can change work required on testnet:
                        hashTarget.SetCompact(block->nBits);
                    }
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

void static ThreadBitcoinMiner(void* parg)
{
    boost::this_thread::interruption_point();
    CWallet* pwallet = (CWallet*)parg;

    try {
        BitcoinMiner(pwallet, false);
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
