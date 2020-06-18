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

#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>


#include "FeeAndPriorityCalculator.h"

using namespace std;

//////////////////////////////////////////////////////////////////////////////
//
// DIVIMiner
//

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest priority or fee rate, so we might consider
// transactions that depend on transactions that aren't yet in the block.
// The COrphan class keeps track of these 'temporary orphans' while
// CreateBlock is figuring out which transactions to include.
//
class COrphan
{
public:
    const CTransaction* ptx;
    set<uint256> setDependsOn;
    CFeeRate feeRate;
    double dPriority;

    COrphan(const CTransaction* ptxIn) : ptx(ptxIn), feeRate(0), dPriority(0)
    {
    }
};

int64_t nLastCoinStakeSearchInterval = 0;

// We want to sort transactions by priority and fee rate, so:
typedef boost::tuple<double, CFeeRate, const CTransaction*> TxPriority;
class TxPriorityCompare
{
    bool byFee;

public:
    TxPriorityCompare(bool _byFee) : byFee(_byFee) {}

    bool operator()(const TxPriority& a, const TxPriority& b)
    {
        if (byFee) {
            if (a.get<1>() == b.get<1>())
                return a.get<0>() < b.get<0>();
            return a.get<1>() < b.get<1>();
        } else {
            if (a.get<0>() == b.get<0>())
                return a.get<1>() < b.get<1>();
            return a.get<0>() < b.get<0>();
        }
    }
};

void UpdateTime(CBlockHeader* block, const CBlockIndex* pindexPrev)
{
    block->nTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());

    // Updating time can change work required on testnet:
    if (Params().AllowMinDifficultyBlocks())
        block->nBits = GetNextWorkRequired(pindexPrev, block);
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

void AddTransactionToBlockAndSetDefaultFees(CBlock& block, unique_ptr<CBlockTemplate>& pblocktemplate, const CMutableTransaction& txNew)
{
    block.vtx.push_back(txNew);
    pblocktemplate->vTxFees.push_back(-1);   // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end
}

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
    block.nBits = GetNextWorkRequired(pindexPrev, &block);
}

int64_t SetBlockTime(CBlock& block)
{
    block.nTime = GetAdjustedTime();
    int64_t nSearchTime = block.nTime; // search to current time
    return nSearchTime;
}
unsigned int GetMaxBlockSize(unsigned int defaultMaxBlockSize, unsigned int maxBlockSizeCurrent)
{
    // Largest block you're willing to create:
    unsigned int blockMaxSize = GetArg("-blockmaxsize", defaultMaxBlockSize);
    // Limit to betweeen 1K and MAX_BLOCK_SIZE-1K for sanity:
    unsigned int blockMaxSizeNetwork = maxBlockSizeCurrent;
    blockMaxSize = std::max((unsigned int)1000, std::min((blockMaxSizeNetwork - 1000), blockMaxSize));
    return blockMaxSize;
}

unsigned int GetBlockPrioritySize(unsigned int defaultBlockPrioritySize, unsigned int blockMaxSize)
{
    unsigned int blockPrioritySize = GetArg("-blockprioritysize", defaultBlockPrioritySize);
    blockPrioritySize = std::min(blockMaxSize, blockPrioritySize);
    return blockPrioritySize;
}

unsigned int GetBlockMinSize(unsigned int defaultBlockMinSize, unsigned int blockMaxSize)
{
    unsigned int blockMinSize = GetArg("-blockminsize", defaultBlockMinSize);
    blockMinSize = std::min(blockMaxSize, blockMinSize);
    return blockMinSize;
}

class BlockMemoryPoolTransactionCollector
{
private: 
    void SetBlockHeaders(CBlock& block, const bool& proofOfStake, const CBlockIndex& indexPrev, unique_ptr<CBlockTemplate>& pblocktemplate)
    {
        block.hashPrevBlock = indexPrev.GetBlockHash();
        if (!proofOfStake)
            UpdateTime(&block, &indexPrev);
        block.nBits = GetNextWorkRequired(&indexPrev, &block);
        block.nNonce = 0;
        block.nAccumulatorCheckpoint = static_cast<uint256>(0);
        pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(block.vtx[0]);
    }

    bool VerifyUTXOIsKnownToMemPool (const CTxMemPool& mempool, const CTxIn& txin, bool& fMissingInputs) 
    {
        if(!mempool.mapTx.count(txin.prevout.hash)){
            LogPrintf("ERROR: mempool transaction missing input\n");
            if (fDebug) assert("mempool transaction missing input" == 0);
            fMissingInputs = true;
            return false;
        }
        return true;
    }
    bool CheckUTXOValidity (const CTxIn& txin, bool& fMissingInputs, const CTransaction &tx) 
    {
        if (mapInvalidOutPoints.count(txin.prevout)) {
            LogPrintf("%s : found invalid input %s in tx %s", __func__, txin.prevout.ToString(), tx.GetHash().ToString());
            fMissingInputs = true;
            return false;
        }
        return true;
    }
    void RecordOrphanTransaction (
        COrphan* porphan, 
        list<COrphan>& vOrphan, 
        const CTransaction& tx, 
        const CTxIn& txin,
        map<uint256, vector<COrphan*> >& mapDependers)
    {
        if (!porphan) {
            // Use list for automatic deletion
            vOrphan.push_back(COrphan(&tx));
            porphan = &vOrphan.back();
        }
        mapDependers[txin.prevout.hash].push_back(porphan);
        porphan->setDependsOn.insert(txin.prevout.hash);
    }

    void ComputeTransactionPriority (
        double& dPriority, 
        const CTransaction& tx, 
        CAmount nTotalIn, 
        COrphan* porphan, 
        vector<TxPriority>& vecPriority,
        const CTransaction* mempoolTx)
    {
        unsigned int nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
        dPriority = FeeAndPriorityCalculator::instance().ComputePriority(tx,dPriority, nTxSize);

        uint256 hash = tx.GetHash();
        mempool.ApplyDeltas(hash, dPriority, nTotalIn);

        CFeeRate feeRate(nTotalIn - tx.GetValueOut(), nTxSize);

        if (porphan) {
            porphan->dPriority = dPriority;
            porphan->feeRate = feeRate;
        } else
            vecPriority.push_back(TxPriority(dPriority, feeRate, mempoolTx));
    }
    void AddDependingTransactionsToPriorityQueue (
        map<uint256, vector<COrphan*> >& mapDependers,
        const uint256& hash,
        vector<TxPriority>& vecPriority,
        TxPriorityCompare& comparer
    )
    {
        if (mapDependers.count(hash)) {
            BOOST_FOREACH (COrphan* porphan, mapDependers[hash]) {
                if (!porphan->setDependsOn.empty()) {
                    porphan->setDependsOn.erase(hash);
                    if (porphan->setDependsOn.empty()) {
                        vecPriority.push_back(TxPriority(porphan->dPriority, porphan->feeRate, porphan->ptx));
                        std::push_heap(vecPriority.begin(), vecPriority.end(), comparer);
                    }
                }
            }
        }
    }
    void SetCoinBaseTransaction (
        CBlock& block, 
        unique_ptr<CBlockTemplate>& pblocktemplate,
        const bool& fProofOfStake, 
        const int& nHeight,
        CMutableTransaction& txNew,
        const CAmount& nFees)
    {
        block.vtx[0].vin[0].scriptSig = CScript() << nHeight << OP_0;
        if (!fProofOfStake) {
            txNew.vout[0].nValue = GetBlockSubsidity(nHeight).nStakeReward;
            txNew.vin[0].scriptSig = CScript() << nHeight << OP_0;
            block.vtx[0] = txNew;
            pblocktemplate->vTxFees[0] = -nFees;
        }
    }
    bool IsFreeTransaction (
        const uint256& hash,
        const bool& fSortedByFee,
        const CFeeRate& feeRate,
        const uint64_t& nBlockSize,
        const unsigned int& nTxSize,
        const unsigned int& nBlockMinSize,
        const CTransaction& tx)
    {
        double dPriorityDelta = 0;
        CAmount nFeeDelta = 0;
        mempool.ApplyDeltas(hash, dPriorityDelta, nFeeDelta);
        
        return (fSortedByFee && 
            (dPriorityDelta <= 0) && 
            (nFeeDelta <= 0) && 
            (feeRate < ::minRelayTxFee) && 
            (nBlockSize + nTxSize >= nBlockMinSize));
    }

    void AddTransactionToBlock (
        CBlock& block,
        const CTransaction& tx, 
        unique_ptr<CBlockTemplate>& pblocktemplate,
        const CAmount& nTxFees,
        const unsigned int& nTxSigOps
        )
    {  
        block.vtx.push_back(tx);
        pblocktemplate->vTxFees.push_back(nTxFees);
        pblocktemplate->vTxSigOps.push_back(nTxSigOps);
    }

    vector<TxPriority> PrioritizeMempoolTransactions (
        const int& nHeight,
        list<COrphan>& vOrphan,
        map<uint256, vector<COrphan*> >& mapDependers,
        CCoinsViewCache& view
        )
    {
        vector<TxPriority> vecPriority;
        vecPriority.reserve(mempool.mapTx.size());
        for (map<uint256, CTxMemPoolEntry>::iterator mi = mempool.mapTx.begin(); mi != mempool.mapTx.end(); ++mi) {
            const CTransaction& tx = mi->second.GetTx();
            if (tx.IsCoinBase() || tx.IsCoinStake() || !IsFinalTx(tx, nHeight)){
                continue;
            }

            COrphan* porphan = NULL;
            double dPriority = 0;
            CAmount nTotalIn = 0;
            bool fMissingInputs = false;
            for (const CTxIn& txin : tx.vin) {

                // Read prev transaction
                if (!view.HaveCoins(txin.prevout.hash)) {
                    // This should never happen; all transactions in the memory
                    // pool should connect to either transactions in the chain
                    // or other transactions in the memory pool.
                    if (!VerifyUTXOIsKnownToMemPool(mempool, txin, fMissingInputs)) {
                        if (porphan)
                            vOrphan.pop_back();
                        break;
                    }

                    // Has to wait for dependencies
                    RecordOrphanTransaction(porphan, vOrphan, tx, txin, mapDependers);

                    nTotalIn += mempool.mapTx[txin.prevout.hash].GetTx().vout[txin.prevout.n].nValue;
                    continue;
                }

                //Check for invalid/fraudulent inputs. They shouldn't make it through mempool, but check anyways.
                if(!CheckUTXOValidity(txin, fMissingInputs, tx)) {
                    break;
                }

                const CCoins* coins = view.AccessCoins(txin.prevout.hash);
                assert(coins);

                CAmount nValueIn = coins->vout[txin.prevout.n].nValue;
                nTotalIn += nValueIn;

                int nConf = nHeight - coins->nHeight;

                dPriority += (double)nValueIn * nConf;
            }
            if (fMissingInputs) { 
                continue;
            }
            ComputeTransactionPriority(dPriority, tx, nTotalIn, porphan, vecPriority, &mi->second.GetTx());
        }
        return vecPriority;
    }

    void PrioritizeFeePastPrioritySize (
        vector<TxPriority>& vecPriority,
        bool& fSortedByFee, 
        TxPriorityCompare& comparer,
        uint64_t& nBlockSize,
        unsigned int& nTxSize,
        unsigned int& nBlockPrioritySize,
        double& dPriority) 
    {
        if (!fSortedByFee &&
                ((nBlockSize + nTxSize >= nBlockPrioritySize) || !AllowFree(dPriority))) {
            fSortedByFee = true;
            comparer = TxPriorityCompare(fSortedByFee);
            std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);
        }
    }

    void AddTransactionsToBlockIfPossible (
        vector<TxPriority>& vecPriority,
        const int& nHeight,
        CCoinsViewCache& view,
        unique_ptr<CBlockTemplate>& pblocktemplate,
        CAmount& nFees,
        map<uint256, vector<COrphan*> >& mapDependers
        )
    {
        // Largest block you're willing to create:
        unsigned int nBlockMaxSize = GetMaxBlockSize(DEFAULT_BLOCK_MAX_SIZE, MAX_BLOCK_SIZE_CURRENT);
        // How much of the block should be dedicated to high-priority transactions,
        // included regardless of the fees they pay
        unsigned int nBlockPrioritySize = GetBlockPrioritySize(DEFAULT_BLOCK_PRIORITY_SIZE, nBlockMaxSize);
        // Minimum block size you want to create; block will be filled with free transactions
        // until there are no more or the block reaches this size:
        unsigned int nBlockMinSize = GetBlockMinSize(DEFAULT_BLOCK_MIN_SIZE, nBlockMaxSize);

        uint64_t nBlockSize = 1000;
        uint64_t nBlockTx = 0;
        int nBlockSigOps = 100;
        bool fSortedByFee = (nBlockPrioritySize <= 0);

        TxPriorityCompare comparer(fSortedByFee);
        std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);

        bool fPrintPriority = GetBoolArg("-printpriority", false);
        CBlock& block = pblocktemplate->block;
        while (!vecPriority.empty()) {
            // Take highest priority transaction off the priority queue:
            double dPriority = vecPriority.front().get<0>();
            CFeeRate feeRate = vecPriority.front().get<1>();
            const CTransaction& tx = *(vecPriority.front().get<2>());

            std::pop_heap(vecPriority.begin(), vecPriority.end(), comparer);
            vecPriority.pop_back();

            // Size limits
            unsigned int nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
            if (nBlockSize + nTxSize >= nBlockMaxSize) {
                continue;
            }

            // Legacy limits on sigOps:
            unsigned int nMaxBlockSigOps = MAX_BLOCK_SIGOPS_CURRENT;
            unsigned int nTxSigOps = GetLegacySigOpCount(tx);
            if (nBlockSigOps + nTxSigOps >= nMaxBlockSigOps) {
                continue;
            }
            
            const uint256& hash = tx.GetHash();
            // Skip free transactions if we're past the minimum block size:
            if(IsFreeTransaction(hash, fSortedByFee, feeRate, nBlockSize, nTxSize, nBlockMinSize, tx))
            {
                continue;
            }
            // Prioritise by fee once past the priority size or we run out of high-priority
            // transactions:
            PrioritizeFeePastPrioritySize(vecPriority, fSortedByFee, comparer, nBlockSize, nTxSize, nBlockPrioritySize, dPriority);

            if (!view.HaveInputs(tx)) {
                continue;
            }


            CAmount nTxFees = view.GetValueIn(tx) - tx.GetValueOut();

            nTxSigOps += GetP2SHSigOpCount(tx, view);
            if (nBlockSigOps + nTxSigOps >= nMaxBlockSigOps) {
                continue;
            }

            // Note that flags: we don't want to set mempool/IsStandard()
            // policy here, but we still have to ensure that the block we
            // create only contains transactions that are valid in new blocks.
            CValidationState state;
            if (!CheckInputs(tx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS, true)) {
                continue;
            }

            CTxUndo txundo;
            UpdateCoins(tx, state, view, txundo, nHeight);

            // Added
            AddTransactionToBlock(block, tx, pblocktemplate, nTxFees, nTxSigOps);
            nBlockSize += nTxSize;
            ++nBlockTx;
            nBlockSigOps += nTxSigOps;
            nFees += nTxFees;
            
            if (fPrintPriority) {
                LogPrintf("priority %.1f fee %s txid %s\n",
                        dPriority, feeRate.ToString(), tx.GetHash().ToString());
            }

            // Add transactions that depend on this one to the priority queue
            AddDependingTransactionsToPriorityQueue(mapDependers, hash, vecPriority, comparer);
        }
        
        LogPrintf("CreateNewBlock(): total size %u\n", nBlockSize);
    }


public:
    bool CollectTransactionsIntoBlock (
        unique_ptr<CBlockTemplate>& pblocktemplate,
        bool& fProofOfStake,
        CMutableTransaction& txNew) 
    {
        
        LOCK2(cs_main, mempool.cs);

        CAmount nFees = 0;
        CBlock& block = pblocktemplate->block;

        CBlockIndex* pindexPrev = chainActive.Tip();
        const int nHeight = pindexPrev->nHeight + 1;
        CCoinsViewCache view(pcoinsTip);

        // Priority order to process transactions
        list<COrphan> vOrphan; // list memory doesn't move
        map<uint256, vector<COrphan*> > mapDependers;

        // This vector will be sorted into a priority queue:
        vector<TxPriority> vecPriority = PrioritizeMempoolTransactions(nHeight, vOrphan, mapDependers, view);

        // Collect transactions into block
        AddTransactionsToBlockIfPossible(
            vecPriority,
            nHeight,
            view,
            pblocktemplate,
            nFees,
            mapDependers
        );

        // Compute final coinbase transaction.
        SetCoinBaseTransaction(block, pblocktemplate, fProofOfStake, nHeight, txNew, nFees);

        // Fill in header
        SetBlockHeaders(block, fProofOfStake, *pindexPrev, pblocktemplate);

        //byrd transaction printout
        LogPrintf("CreateNewBlock(): block tostring %s\n", block.ToString());

        CValidationState state;
        if (!TestBlockValidity(state, block, pindexPrev, false, false)) {
            LogPrintf("CreateNewBlock() : TestBlockValidity failed\n");
            mempool.clear();
            return false;
        }
        LogPrintf("CreateNewBlock(): validation passed %s\n", "");
        return true;
    }
};

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

    if(!BlockMemoryPoolTransactionCollector ()
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
                        MilliSleep(5000);
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
