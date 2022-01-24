#include <BlockMemoryPoolTransactionCollector.h>

#include <BlockTemplate.h>
#include "chain.h"
#include "coins.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "txmempool.h"
#include <ValidationState.h>
#include <defaultValues.h>
#include <Logging.h>
#include <TransactionOpCounting.h>
#include <UtxoCheckingAndUpdating.h>

#include <Settings.h>

bool IsFinalTx(const CTransaction& tx, const CChain& activeChain, int nBlockHeight = 0 , int64_t nBlockTime = 0);

static unsigned int GetMaxBlockSize(const Settings& settings,unsigned int defaultMaxBlockSize, unsigned int maxBlockSizeCurrent)
{
    // Largest block you're willing to create:
    unsigned int blockMaxSize = settings.GetArg("-blockmaxsize", defaultMaxBlockSize);
    // Limit to betweeen 1K and MAX_BLOCK_SIZE-1K for sanity:
    unsigned int blockMaxSizeNetwork = maxBlockSizeCurrent;
    unsigned int blockMaxSizeNetworkMinus1k = (blockMaxSizeNetwork - 1000);
    blockMaxSize = std::max((unsigned int)1000, std::min(blockMaxSizeNetworkMinus1k, blockMaxSize));
    return blockMaxSize;
}

static unsigned int GetBlockPrioritySize(const Settings& settings, unsigned int defaultBlockPrioritySize, unsigned int blockMaxSize)
{
    unsigned int blockPrioritySize = settings.GetArg("-blockprioritysize", defaultBlockPrioritySize);
    blockPrioritySize = std::min(blockMaxSize, blockPrioritySize);
    return blockPrioritySize;
}

static unsigned int GetBlockMinSize(const Settings& settings, unsigned int defaultBlockMinSize, unsigned int blockMaxSize)
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
    double coinAgeOfInputsPerByte;
    CAmount feePaid;
    size_t txSize;

    COrphan(const CTransaction* ptxIn) : ptx(ptxIn), feeRate(0), coinAgeOfInputsPerByte(0), feePaid(0), txSize(0u)
    {
    }
};

class TxPriorityCompare
{
    bool byFee;

public:
    TxPriorityCompare(bool _byFee) : byFee(_byFee) {}

    void sortByFee(){ byFee = true;}

    bool operator()(const TxPriority& a, const TxPriority& b)
    {
        if (byFee) {
            if (a._nominalFeeRate == b._nominalFeeRate)
                return a._coinAgeOfInputsPerByte < b._coinAgeOfInputsPerByte;
            return a._nominalFeeRate < b._nominalFeeRate;
        } else {
            if (a._coinAgeOfInputsPerByte == b._coinAgeOfInputsPerByte)
                return a._nominalFeeRate < b._nominalFeeRate;
            return a._coinAgeOfInputsPerByte < b._coinAgeOfInputsPerByte;
        }
    }
};

BlockMemoryPoolTransactionCollector::BlockMemoryPoolTransactionCollector(
    const Settings& settings,
    const CCoinsViewCache* baseCoinsViewCache,
    const CChain& activeChain,
    const BlockMap& blockIndexMap,
    CTxMemPool& mempool,
    CCriticalSection& mainCS,
    const CFeeRate& txFeeRate
    ): baseCoinsViewCache_(baseCoinsViewCache)
    , activeChain_(activeChain)
    , blockIndexMap_(blockIndexMap)
    , mempool_(mempool)
    , mainCS_(mainCS)
    , txFeeRate_(txFeeRate)
    , blockMaxSize_(GetMaxBlockSize(settings,DEFAULT_BLOCK_MAX_SIZE, MAX_BLOCK_SIZE_CURRENT))
    , blockPrioritySize_(GetBlockPrioritySize(settings,DEFAULT_BLOCK_PRIORITY_SIZE, blockMaxSize_))
    , blockMinSize_(GetBlockMinSize(settings,DEFAULT_BLOCK_MIN_SIZE, blockMaxSize_))
{
    assert( blockMaxSize_ <= MAX_BLOCK_SIZE_CURRENT );
}

void BlockMemoryPoolTransactionCollector::RecordOrphanTransaction(
    std::shared_ptr<COrphan>& porphan,
    const CTransaction& tx,
    const CTxIn& txin,
    DependingTransactionsMap& dependentTransactions) const
{
    if (porphan == nullptr)
        porphan = std::make_shared<COrphan>(&tx);
    dependentTransactions[txin.prevout.hash].push_back(porphan);
    porphan->setDependsOn.insert(txin.prevout.hash);
}

void BlockMemoryPoolTransactionCollector::ComputeTransactionPriority(
    const CTxMemPoolEntry& mempoolTx,
    const int nHeight,
    COrphan* porphan,
    std::vector<TxPriority>& vecPriority) const
{
    const size_t transactionSize = mempoolTx.GetTxSize();
    const CTransaction& tx = mempoolTx.GetTx();
    const CAmount feePaid = mempoolTx.GetFee();
    const CAmount valueSent = tx.GetValueOut();
    double coinAgeOfInputsPerByte = mempoolTx.ComputeInputCoinAgePerByte(nHeight);
    CAmount nTotalIn = valueSent + feePaid;

    uint256 hash = tx.GetHash();
    mempool_.ApplyDeltas(hash, coinAgeOfInputsPerByte, nTotalIn);
    CAmount nominalFee = nTotalIn - valueSent;
    CFeeRate feeRate(nominalFee, transactionSize);

    if (porphan) {
        porphan->coinAgeOfInputsPerByte = coinAgeOfInputsPerByte;
        porphan->feeRate = feeRate;
        porphan->feePaid = feePaid;
        porphan->txSize = transactionSize;
    } else
        vecPriority.push_back(TxPriority(coinAgeOfInputsPerByte, feeRate, &tx,feePaid,transactionSize));
}

void BlockMemoryPoolTransactionCollector::AddDependingTransactionsToPriorityQueue(
    DependingTransactionsMap& dependentTransactions,
    const uint256& hash,
    std::vector<TxPriority>& vecPriority,
    TxPriorityCompare& comparer) const
{
    if (dependentTransactions.count(hash)) {
        for (auto& porphan : dependentTransactions[hash]) {
            if (!porphan->setDependsOn.empty()) {
                porphan->setDependsOn.erase(hash);
                if (porphan->setDependsOn.empty()) {
                    vecPriority.push_back(
                        TxPriority(porphan->coinAgeOfInputsPerByte, porphan->feeRate, porphan->ptx,porphan->feePaid,porphan->txSize)
                        );
                    std::push_heap(vecPriority.begin(), vecPriority.end(), comparer);
                }
            }
        }
    }
}

bool BlockMemoryPoolTransactionCollector::ShouldSkipCheapTransaction(
    const CFeeRate& feeRate,
    const uint64_t currentBlockSize,
    const unsigned int transactionSize) const
{
    return feeRate < txFeeRate_ &&
        (currentBlockSize + transactionSize >= blockMinSize_);
}

void BlockMemoryPoolTransactionCollector::AddTransactionToBlock(
    const CTransaction& tx,
    const CAmount feePaid,
    CBlock& block) const
{
    if(block.IsProofOfWork())
    {
        assert(feePaid >= 0);
        block.vtx[0].vout[0].nValue += feePaid;
    }
    block.vtx.push_back(tx);
}

std::vector<TxPriority> BlockMemoryPoolTransactionCollector::ComputeMempoolTransactionPriorities(
    const int& nHeight,
    DependingTransactionsMap& dependentTransactions,
    const CCoinsViewCache& view) const
{
    std::vector<TxPriority> vecPriority;
    vecPriority.reserve(mempool_.mapTx.size());
    for (auto mi = mempool_.mapTx.begin(); mi != mempool_.mapTx.end(); ++mi) {
        const CTransaction& tx = mi->second.GetTx();
        if (tx.IsCoinBase() || tx.IsCoinStake() || !IsFinalTx(tx, activeChain_, nHeight)){
            continue;
        }

        std::shared_ptr<COrphan> porphan;
        bool fMissingInputs = false;
        for (const CTxIn& txin : tx.vin) {

            // Read prev transaction
            if (!view.HaveCoins(txin.prevout.hash)) {
                CTransaction prevTx;
                if(!mempool_.lookupOutpoint(txin.prevout.hash, prevTx)) {
                    // This should never happen; all transactions in the memory
                    // pool should connect to either transactions in the chain
                    // or other transactions in the memory pool.
                    LogPrintf("ERROR: %s - mempool transaction missing input\n",__func__);
                    fMissingInputs = true;
                    break;
                }

                // Has to wait for dependencies
                RecordOrphanTransaction(porphan, tx, txin, dependentTransactions);
                continue;
            }

            const CCoins* coins = view.AccessCoins(txin.prevout.hash);
            assert(coins);
        }
        if (fMissingInputs) {
            continue;
        }
        ComputeTransactionPriority(mi->second, nHeight, porphan.get(), vecPriority);
    }
    return vecPriority;
}

bool BlockMemoryPoolTransactionCollector::ShouldSwitchToPriotizationByFee(
    const uint64_t& currentBlockSize,
    const unsigned int& transactionSize,
    const bool mustPayFees) const
{
    return (currentBlockSize + transactionSize >= blockPrioritySize_) || mustPayFees;
}

PrioritizedTransactionData::PrioritizedTransactionData(
    ): tx(nullptr)
    , transactionSigOpCount(0u)
    , fee(0)
{
}
PrioritizedTransactionData::PrioritizedTransactionData(
    const CTransaction& transaction,
    unsigned transactionSigOps,
    CAmount feePaid
    ): tx(&transaction)
    , transactionSigOpCount(transactionSigOps)
    , fee(feePaid)
{
}

std::vector<PrioritizedTransactionData> BlockMemoryPoolTransactionCollector::PrioritizeTransactionsByBlockSpaceUsage(
    std::vector<TxPriority>& vecPriority,
    const int& nHeight,
    CCoinsViewCache& view,
    DependingTransactionsMap& dependentTransactions) const
{
    std::vector<PrioritizedTransactionData> prioritizedTransactions;
    uint64_t currentBlockSize = 1000;
    int currentBlockSigOps = 100;
    const unsigned int constexpr maximumSigOpsPerBlock = MAX_BLOCK_SIGOPS_CURRENT;
    bool txsArePrioritizedByFeePaid = (blockPrioritySize_ <= 0);

    TxPriorityCompare comparer(txsArePrioritizedByFeePaid);
    std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);

    while (!vecPriority.empty()) {
        // Take highest priority transaction off the priority queue:
        const auto& priorityDatum = vecPriority.front();
        const bool mustPayFees = !CTxMemPoolEntry::AllowFree(priorityDatum._coinAgeOfInputsPerByte);
        CFeeRate feeRate = priorityDatum._nominalFeeRate;
        const CTransaction& tx = *(priorityDatum._transactionRef);
        const CAmount fee = priorityDatum._feePaid;
        const unsigned transactionSize = priorityDatum._transactionSize;

        std::pop_heap(vecPriority.begin(), vecPriority.end(), comparer);
        vecPriority.pop_back();

        // Legacy limits on sigOps:
        unsigned int transactionSigOpCount = GetLegacySigOpCount(tx);
        const uint256& hash = tx.GetHash();
        // Skip free transactions if we're past the minimum block size:
        if (currentBlockSize + transactionSize >= blockMaxSize_ ||
            currentBlockSigOps + transactionSigOpCount >= maximumSigOpsPerBlock)
        {
            continue;
        }
        // Prioritise by fee once past the priority size or we run out of high-priority
        // transactions:
        if(!txsArePrioritizedByFeePaid)
        {
            if(ShouldSwitchToPriotizationByFee(currentBlockSize, transactionSize, mustPayFees))
            {
                txsArePrioritizedByFeePaid = true;
                comparer.sortByFee();
                std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);
            }
        }
        else if( !mempool_.IsPrioritizedTransaction(hash) && ShouldSkipCheapTransaction(feeRate, currentBlockSize, transactionSize))
        {
            continue;
        }

        if (!view.HaveInputs(tx)) {
            continue;
        }
        transactionSigOpCount += GetP2SHSigOpCount(tx, view);
        if (currentBlockSigOps + transactionSigOpCount >= maximumSigOpsPerBlock) {
            continue;
        }

        // Note that flags: we don't want to set mempool/IsStandard()
        // policy here, but we still have to ensure that the block we
        // create only contains transactions that are valid in new blocks.
        CValidationState state;
        if (!CheckInputs(tx, state, view, blockIndexMap_, true, MANDATORY_SCRIPT_VERIFY_FLAGS)) {
            continue;
        }

        prioritizedTransactions.emplace_back(tx, transactionSigOpCount,fee);
        currentBlockSize += transactionSize;
        currentBlockSigOps += transactionSigOpCount;

        CTxUndo txundo;
        view.UpdateWithConfirmedTransaction(tx,nHeight,txundo);

        // Add transactions that depend on this one to the priority queue
        AddDependingTransactionsToPriorityQueue(dependentTransactions, hash, vecPriority, comparer);
    }

    LogPrintf("%s: total size %u\n",__func__, currentBlockSize);
    return prioritizedTransactions;
}

void BlockMemoryPoolTransactionCollector::AddTransactionsToBlockIfPossible(
    const int& nHeight,
    CCoinsViewCache& view,
    CBlock& block) const
{
    DependingTransactionsMap dependentTransactions;

    std::vector<TxPriority> vecPriority =
        ComputeMempoolTransactionPriorities(nHeight, dependentTransactions, view);

    std::vector<PrioritizedTransactionData> prioritizedTransactions =
        PrioritizeTransactionsByBlockSpaceUsage(
            vecPriority,
            nHeight,
            view,
            dependentTransactions);

    for(const PrioritizedTransactionData& txData: prioritizedTransactions)
    {
        const CTransaction& tx = *txData.tx;
        AddTransactionToBlock(tx, txData.fee, block);
    }
    if(block.IsProofOfWork()) block.vtx[0] = CMutableTransaction(block.vtx[0]);
}

bool BlockMemoryPoolTransactionCollector::CollectTransactionsIntoBlock(
    CBlockTemplate& pblocktemplate) const
{
    LOCK2(mainCS_, mempool_.cs);
    if(pblocktemplate.previousBlockIndex != activeChain_.Tip()) return false;

    CBlock& block = pblocktemplate.block;
    if(block.vtx.size() < 1) return false; // Block reward transaction must be set first
    const int nHeight = pblocktemplate.previousBlockIndex->nHeight + 1;
    CCoinsViewCache view(baseCoinsViewCache_);

    AddTransactionsToBlockIfPossible(nHeight, view, block);

    LogPrintf("%s: block tostring %s\n",__func__, block);
    return true;
}
