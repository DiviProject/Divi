#include <BlockMemoryPoolTransactionCollector.h>

#include <BlockTemplate.h>
#include "chain.h"
#include "coins.h"
#include "FeeAndPriorityCalculator.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "txmempool.h"
#include <ValidationState.h>
#include <defaultValues.h>
#include <Logging.h>
#include <TransactionOpCounting.h>
#include <UtxoCheckingAndUpdating.h>

#include <Settings.h>
extern Settings& settings;

extern CChain chainActive;
extern CCoinsViewCache* pcoinsTip;

bool IsFinalTx(const CTransaction& tx, const CChain& activeChain, int nBlockHeight = 0 , int64_t nBlockTime = 0);

static unsigned int GetMaxBlockSize(unsigned int defaultMaxBlockSize, unsigned int maxBlockSizeCurrent)
{
    // Largest block you're willing to create:
    unsigned int blockMaxSize = settings.GetArg("-blockmaxsize", defaultMaxBlockSize);
    // Limit to betweeen 1K and MAX_BLOCK_SIZE-1K for sanity:
    unsigned int blockMaxSizeNetwork = maxBlockSizeCurrent;
    blockMaxSize = std::max((unsigned int)1000, std::min((blockMaxSizeNetwork - 1000), blockMaxSize));
    return blockMaxSize;
}

static unsigned int GetBlockPrioritySize(unsigned int defaultBlockPrioritySize, unsigned int blockMaxSize)
{
    unsigned int blockPrioritySize = settings.GetArg("-blockprioritysize", defaultBlockPrioritySize);
    blockPrioritySize = std::min(blockMaxSize, blockPrioritySize);
    return blockPrioritySize;
}

static unsigned int GetBlockMinSize(unsigned int defaultBlockMinSize, unsigned int blockMaxSize)
{
    unsigned int blockMinSize = settings.GetArg("-blockminsize", defaultBlockMinSize);
    blockMinSize = std::min(blockMaxSize, blockMinSize);
    return blockMinSize;
}

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
    std::set<uint256> setDependsOn;
    CFeeRate feeRate;
    double dPriority;

    COrphan(const CTransaction* ptxIn) : ptx(ptxIn), feeRate(0), dPriority(0)
    {
    }
};

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

BlockMemoryPoolTransactionCollector::BlockMemoryPoolTransactionCollector(
    const CChain& activeChain,
    CTxMemPool& mempool,
    CCriticalSection& mainCS,
    const CFeeRate& txFeeRate
    ): activeChain_(activeChain)
    , mempool_(mempool)
    , mainCS_(mainCS)
    , txFeeRate_(txFeeRate)
    , nBlockMaxSize(GetMaxBlockSize(DEFAULT_BLOCK_MAX_SIZE, MAX_BLOCK_SIZE_CURRENT))
    , nBlockPrioritySize(GetBlockPrioritySize(DEFAULT_BLOCK_PRIORITY_SIZE, nBlockMaxSize))
    , nBlockMinSize(GetBlockMinSize(DEFAULT_BLOCK_MIN_SIZE, nBlockMaxSize))
{

}

void BlockMemoryPoolTransactionCollector::RecordOrphanTransaction (
    std::shared_ptr<COrphan>& porphan,
    const CTransaction& tx,
    const CTxIn& txin,
    std::map<uint256, std::vector<std::shared_ptr<COrphan>>>& dependentTransactions) const
{
    if (porphan == nullptr)
        porphan = std::make_shared<COrphan>(&tx);
    dependentTransactions[txin.prevout.hash].push_back(porphan);
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
    mempool_.ApplyDeltas(hash, dPriority, nTotalIn);

    CFeeRate feeRate(nTotalIn - tx.GetValueOut(), nTxSize);

    if (porphan) {
        porphan->dPriority = dPriority;
        porphan->feeRate = feeRate;
    } else
        vecPriority.push_back(TxPriority(dPriority, feeRate, mempoolTx));
}

void BlockMemoryPoolTransactionCollector::AddDependingTransactionsToPriorityQueue (
    std::map<uint256, std::vector<std::shared_ptr<COrphan>>>& dependentTransactions,
    const uint256& hash,
    std::vector<TxPriority>& vecPriority,
    TxPriorityCompare& comparer) const
{
    if (dependentTransactions.count(hash)) {
        for (auto& porphan : dependentTransactions[hash]) {
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

bool BlockMemoryPoolTransactionCollector::IsFreeTransaction (
    const uint256& hash,
    const bool& fSortedByFee,
    const CFeeRate& feeRate,
    const uint64_t& nBlockSize,
    const unsigned int& nTxSize,
    const CTransaction& tx) const
{
    double dPriorityDelta = 0;
    CAmount nFeeDelta = 0;
    mempool_.ApplyDeltas(hash, dPriorityDelta, nFeeDelta);

    return (fSortedByFee &&
        (dPriorityDelta <= 0) &&
        (nFeeDelta <= 0) &&
        (feeRate < txFeeRate_) &&
        (nBlockSize + nTxSize >= nBlockMinSize));
}

void BlockMemoryPoolTransactionCollector::AddTransactionToBlock (
    const CTransaction& tx,
    CBlockTemplate& blocktemplate) const
{
    blocktemplate.block.vtx.push_back(tx);
}

std::vector<TxPriority> BlockMemoryPoolTransactionCollector::PrioritizeMempoolTransactions (
    const int& nHeight,
    std::map<uint256, std::vector<std::shared_ptr<COrphan>>>& dependentTransactions,
    CCoinsViewCache& view) const
{
    std::vector<TxPriority> vecPriority;
    vecPriority.reserve(mempool_.mapTx.size());
    for (auto mi = mempool_.mapTx.begin(); mi != mempool_.mapTx.end(); ++mi) {
        const CTransaction& tx = mi->second.GetTx();
        if (tx.IsCoinBase() || tx.IsCoinStake() || !IsFinalTx(tx, chainActive, nHeight)){
            continue;
        }

        std::shared_ptr<COrphan> porphan;
        double dPriority = 0;
        CAmount nTotalIn = 0;
        bool fMissingInputs = false;
        for (const CTxIn& txin : tx.vin) {

            // Read prev transaction
            if (!view.HaveCoins(txin.prevout.hash)) {
                CTransaction prevTx;
                if(!mempool_.lookup(txin.prevout.hash, prevTx)) {
                    // This should never happen; all transactions in the memory
                    // pool should connect to either transactions in the chain
                    // or other transactions in the memory pool.
                    LogPrintf("ERROR: mempool transaction missing input\n");
                    fMissingInputs = true;
                    break;
                }

                // Has to wait for dependencies
                RecordOrphanTransaction(porphan, tx, txin, dependentTransactions);

                nTotalIn += prevTx.vout[txin.prevout.n].nValue;
                continue;
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
        ComputeTransactionPriority(dPriority, tx, nTotalIn, porphan.get(), vecPriority, &mi->second.GetTx());
    }
    return vecPriority;
}

void BlockMemoryPoolTransactionCollector::PrioritizeFeePastPrioritySize
(
    std::vector<TxPriority>& vecPriority,
    bool& fSortedByFee,
    TxPriorityCompare& comparer,
    const uint64_t& nBlockSize,
    const unsigned int& nTxSize,
    double& dPriority) const
{
    if (!fSortedByFee &&
            ((nBlockSize + nTxSize >= nBlockPrioritySize) || !AllowFree(dPriority))) {
        fSortedByFee = true;
        comparer = TxPriorityCompare(fSortedByFee);
        std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);
    }
}

PrioritizedTransactionData::PrioritizedTransactionData(
    ): tx(nullptr)
    , nTxSigOps(0u)
{
}
PrioritizedTransactionData::PrioritizedTransactionData(
    const CTransaction& transaction,
    unsigned txSigOps
    ): tx(&transaction)
    , nTxSigOps(txSigOps)
{
}

std::vector<PrioritizedTransactionData> BlockMemoryPoolTransactionCollector::PrioritizeTransactions(
    std::vector<TxPriority>& vecPriority,
    const int& nHeight,
    CCoinsViewCache& view,
    std::map<uint256, std::vector<std::shared_ptr<COrphan>>>& dependentTransactions) const
{
    std::vector<PrioritizedTransactionData> prioritizedTransactions;

    // Largest block you're willing to create:
    // How much of the block should be dedicated to high-priority transactions,
    // included regardless of the fees they pay
    // Minimum block size you want to create; block will be filled with free transactions
    // until there are no more or the block reaches this size:

    uint64_t nBlockSize = 1000;
    int nBlockSigOps = 100;
    const unsigned int constexpr nMaxBlockSigOps = MAX_BLOCK_SIGOPS_CURRENT;
    bool fSortedByFee = (nBlockPrioritySize <= 0);

    TxPriorityCompare comparer(fSortedByFee);
    std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);

    bool fPrintPriority = settings.GetBoolArg("-printpriority", false);
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
            IsFreeTransaction(hash, fSortedByFee, feeRate, nBlockSize, nTxSize, tx))
        {
            continue;
        }
        // Prioritise by fee once past the priority size or we run out of high-priority
        // transactions:
        PrioritizeFeePastPrioritySize(vecPriority, fSortedByFee, comparer, nBlockSize, nTxSize, dPriority);
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

        prioritizedTransactions.emplace_back(tx, nTxSigOps);
        nBlockSize += nTxSize;
        nBlockSigOps += nTxSigOps;

        CTxUndo txundo;
        UpdateCoinsWithTransaction(tx, view, txundo, nHeight);

        if (fPrintPriority) {
            LogPrintf("priority %.1f fee %s txid %s\n",
                    dPriority, feeRate, tx.GetHash());
        }

        // Add transactions that depend on this one to the priority queue
        AddDependingTransactionsToPriorityQueue(dependentTransactions, hash, vecPriority, comparer);
    }

    LogPrintf("CreateNewBlock(): total size %u\n", nBlockSize);
    return prioritizedTransactions;
}

void BlockMemoryPoolTransactionCollector::AddTransactionsToBlockIfPossible (
    const int& nHeight,
    CCoinsViewCache& view,
    CBlockTemplate& blocktemplate) const
{
    std::map<uint256, std::vector<std::shared_ptr<COrphan>>> dependentTransactions;

    std::vector<TxPriority> vecPriority =
        PrioritizeMempoolTransactions(nHeight, dependentTransactions, view);

    std::vector<PrioritizedTransactionData> prioritizedTransactions =
        PrioritizeTransactions(
            vecPriority,
            nHeight,
            view,
            dependentTransactions);

    for(const PrioritizedTransactionData& txData: prioritizedTransactions)
    {
        const CTransaction& tx = *txData.tx;
        AddTransactionToBlock(tx, blocktemplate);
    }
}

bool BlockMemoryPoolTransactionCollector::CollectTransactionsIntoBlock (
    CBlockTemplate& pblocktemplate) const
{
    LOCK2(mainCS_, mempool_.cs);

    CBlock& block = pblocktemplate.block;

    pblocktemplate.previousBlockIndex = activeChain_.Tip();
    const int nHeight = pblocktemplate.previousBlockIndex->nHeight + 1;
    CCoinsViewCache view(pcoinsTip);

    AddTransactionsToBlockIfPossible(
        nHeight,
        view,
        pblocktemplate);

    LogPrintf("CreateNewBlock(): block tostring %s\n", block);
    return true;
}
