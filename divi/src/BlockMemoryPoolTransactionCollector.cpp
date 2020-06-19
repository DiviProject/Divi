#include <BlockMemoryPoolTransactionCollector.h>

#include "chain.h"
#include "coins.h"
#include "FeeAndPriorityCalculator.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "pow.h"
#include "SuperblockHelpers.h"
#include "txmempool.h"
#include "utiltime.h"
#include "timedata.h"
#include "util.h"
#include "wallet.h"

int64_t nLastCoinStakeSearchInterval = 0;

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


void BlockMemoryPoolTransactionCollector::UpdateTime(CBlockHeader* block, const CBlockIndex* pindexPrev) const
{
    block->nTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());

    // Updating time can change work required on testnet:
    if (Params().AllowMinDifficultyBlocks())
        block->nBits = GetNextWorkRequired(pindexPrev, block,Params());
}

void BlockMemoryPoolTransactionCollector::SetBlockHeaders(CBlock& block, const bool& proofOfStake, const CBlockIndex& indexPrev, std::unique_ptr<CBlockTemplate>& pblocktemplate) const
{
    // Fill in header
    block.hashPrevBlock = indexPrev.GetBlockHash();
    if (!proofOfStake)
        UpdateTime(&block, &indexPrev);
    block.nBits = GetNextWorkRequired(&indexPrev, &block, Params());
    block.nNonce = 0;
    block.nAccumulatorCheckpoint = static_cast<uint256>(0);
    pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(block.vtx[0]);
}

bool BlockMemoryPoolTransactionCollector::VerifyUTXOIsKnownToMemPool (const CTxMemPool& mempool, const CTxIn& txin, bool& fMissingInputs)  const
{
    if(!mempool.mapTx.count(txin.prevout.hash)){
        LogPrintf("ERROR: mempool transaction missing input\n");
        if (fDebug) assert("mempool transaction missing input" == 0);
        fMissingInputs = true;
        return false;
    }
    return true;
}
bool BlockMemoryPoolTransactionCollector::CheckUTXOValidity (const CTxIn& txin, bool& fMissingInputs, const CTransaction &tx)  const
{
    if (mapInvalidOutPoints.count(txin.prevout)) {
        LogPrintf("%s : found invalid input %s in tx %s", __func__, txin.prevout.ToString(), tx.GetHash().ToString());
        fMissingInputs = true;
        return false;
    }
    return true;
}
void BlockMemoryPoolTransactionCollector::RecordOrphanTransaction (
    COrphan* porphan, 
    std::list<COrphan>& vOrphan, 
    const CTransaction& tx, 
    const CTxIn& txin,
    std::map<uint256, std::vector<COrphan*> >& mapDependers) const
{
    if (!porphan) {
        // Use list for automatic deletion
        vOrphan.push_back(COrphan(&tx));
        porphan = &vOrphan.back();
    }
    mapDependers[txin.prevout.hash].push_back(porphan);
    porphan->setDependsOn.insert(txin.prevout.hash);
}

void BlockMemoryPoolTransactionCollector::ComputeTransactionPriority (
    double& dPriority, 
    const CTransaction& tx, 
    CAmount nTotalIn, 
    COrphan* porphan, 
    std::vector<TxPriority>& vecPriority,
    const CTransaction* mempoolTx) const
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

void BlockMemoryPoolTransactionCollector::AddDependingTransactionsToPriorityQueue (
    std::map<uint256, std::vector<COrphan*> >& mapDependers,
    const uint256& hash,
    std::vector<TxPriority>& vecPriority,
    TxPriorityCompare& comparer) const
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
void BlockMemoryPoolTransactionCollector::SetCoinBaseTransaction (
    CBlock& block, 
    std::unique_ptr<CBlockTemplate>& pblocktemplate,
    const bool& fProofOfStake, 
    const int& nHeight,
    CMutableTransaction& txNew,
    const CAmount& nFees) const
{
    // Compute final coinbase transaction.
    block.vtx[0].vin[0].scriptSig = CScript() << nHeight << OP_0;
    if (!fProofOfStake) {
        txNew.vout[0].nValue = GetBlockSubsidity(nHeight).nStakeReward;
        txNew.vin[0].scriptSig = CScript() << nHeight << OP_0;
        block.vtx[0] = txNew;
        pblocktemplate->vTxFees[0] = -nFees;
    }
}
bool BlockMemoryPoolTransactionCollector::IsFreeTransaction (
    const uint256& hash,
    const bool& fSortedByFee,
    const CFeeRate& feeRate,
    const uint64_t& nBlockSize,
    const unsigned int& nTxSize,
    const unsigned int& nBlockMinSize,
    const CTransaction& tx) const
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

void BlockMemoryPoolTransactionCollector::AddTransactionToBlock (
    CBlock& block,
    const CTransaction& tx, 
    std::unique_ptr<CBlockTemplate>& pblocktemplate,
    const CAmount& nTxFees,
    const unsigned int& nTxSigOps) const
{  
    block.vtx.push_back(tx);
    pblocktemplate->vTxFees.push_back(nTxFees);
    pblocktemplate->vTxSigOps.push_back(nTxSigOps);
}

std::vector<TxPriority> BlockMemoryPoolTransactionCollector::PrioritizeMempoolTransactions (
    const int& nHeight,
    std::map<uint256, std::vector<COrphan*> >& mapDependers,
    CCoinsViewCache& view) const
{
    std::list<COrphan> vOrphan;
    std::vector<TxPriority> vecPriority;
    vecPriority.reserve(mempool.mapTx.size());
    for (std::map<uint256, CTxMemPoolEntry>::iterator mi = mempool.mapTx.begin(); mi != mempool.mapTx.end(); ++mi) {
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

void BlockMemoryPoolTransactionCollector::PrioritizeFeePastPrioritySize (
    std::vector<TxPriority>& vecPriority,
    bool& fSortedByFee, 
    TxPriorityCompare& comparer,
    const uint64_t& nBlockSize,
    const unsigned int& nTxSize,
    const unsigned int& nBlockPrioritySize,
    double& dPriority) const 
{
    if (!fSortedByFee &&
            ((nBlockSize + nTxSize >= nBlockPrioritySize) || !AllowFree(dPriority))) {
        fSortedByFee = true;
        comparer = TxPriorityCompare(fSortedByFee);
        std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);
    }
}

void BlockMemoryPoolTransactionCollector::AddTransactionsToBlockIfPossible (
    std::vector<TxPriority>& vecPriority,
    const int& nHeight,
    CCoinsViewCache& view,
    std::unique_ptr<CBlockTemplate>& pblocktemplate,
    CAmount& nFees,
    std::map<uint256, std::vector<COrphan*> >& mapDependers) const
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
    const unsigned int constexpr nMaxBlockSigOps = MAX_BLOCK_SIGOPS_CURRENT;
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
        // Legacy limits on sigOps:
        unsigned int nTxSigOps = GetLegacySigOpCount(tx);
        // Skip free transactions if we're past the minimum block size:
        const uint256& hash = tx.GetHash();
        if (nBlockSize + nTxSize >= nBlockMaxSize || 
            nBlockSigOps + nTxSigOps >= nMaxBlockSigOps||
            IsFreeTransaction(hash, fSortedByFee, feeRate, nBlockSize, nTxSize, nBlockMinSize, tx))
        {
            continue;
        }
        // Prioritise by fee once past the priority size or we run out of high-priority
        // transactions:
        PrioritizeFeePastPrioritySize(vecPriority, fSortedByFee, comparer, nBlockSize, nTxSize, nBlockPrioritySize, dPriority);
        if (!view.HaveInputs(tx)) {
            continue;
        }
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

        CAmount nTxFees = view.GetValueIn(tx) - tx.GetValueOut();
        CTxUndo txundo;
        UpdateCoins(tx, view, txundo, nHeight);

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

bool BlockMemoryPoolTransactionCollector::CollectTransactionsIntoBlock (
    std::unique_ptr<CBlockTemplate>& pblocktemplate,
    bool& fProofOfStake,
    CMutableTransaction& txNew) const
{
    
    LOCK2(cs_main, mempool.cs);

    CAmount nFees = 0;
    CBlock& block = pblocktemplate->block;

    CBlockIndex* pindexPrev = chainActive.Tip();
    const int nHeight = pindexPrev->nHeight + 1;
    CCoinsViewCache view(pcoinsTip);

    // Priority order to process transactions
    std::map<uint256, std::vector<COrphan*> > mapDependers;

    // This vector will be sorted into a priority queue:
    std::vector<TxPriority> vecPriority = PrioritizeMempoolTransactions(nHeight, mapDependers, view);

    // Collect transactions into block
    AddTransactionsToBlockIfPossible(
        vecPriority,
        nHeight,
        view,
        pblocktemplate,
        nFees,
        mapDependers
    );

    SetCoinBaseTransaction(block, pblocktemplate, fProofOfStake, nHeight, txNew, nFees);
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