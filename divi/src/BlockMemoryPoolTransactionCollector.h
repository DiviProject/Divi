#ifndef BLOCK_MEMORY_POOL_TRANSACTION_COLLECTOR_H
#define BLOCK_MEMORY_POOL_TRANSACTION_COLLECTOR_H

#include <amount.h>
#include <FeeRate.h>
#include <uint256.h>

#include <list>
#include <memory>
#include <set>
#include <stdint.h>
#include <vector>

#include <boost/tuple/tuple.hpp>
#include <boost/thread/recursive_mutex.hpp>

#include <I_BlockTransactionCollector.h>

class CTransaction;
class CTxIn;
class CBlock;
class CCoinsViewCache;
class CBlockIndex;
class CTxMemPool;
class CBlockTemplate;
class CBlockHeader;
class CFeeRate;

template <typename MutexObj>
class AnnotatedMixin;

struct PrioritizedTransactionData
{
    const CTransaction* tx;
    unsigned int nTxSigOps;
    PrioritizedTransactionData();
    PrioritizedTransactionData(
        const CTransaction& transaction,
        unsigned txSigOps);
};

class COrphan;

// We want to sort transactions by priority and fee rate, so:
typedef boost::tuple<double, CFeeRate, const CTransaction*> TxPriority;
class TxPriorityCompare;
class CChain;

class BlockMemoryPoolTransactionCollector: public I_BlockTransactionCollector
{
private:
    const CChain& activeChain_;
    CTxMemPool& mempool_;
    AnnotatedMixin<boost::recursive_mutex>& mainCS_;
    const CFeeRate& txFeeRate_;
private:
    void UpdateTime(CBlockHeader* block, const CBlockIndex* pindexPrev) const;
    void RecordOrphanTransaction (
        std::shared_ptr<COrphan>& porphan,
        const CTransaction& tx,
        const CTxIn& txin,
        std::map<uint256, std::vector<std::shared_ptr<COrphan>>>& mapDependers) const;

    void ComputeTransactionPriority (
        double& dPriority,
        const CTransaction& tx,
        CAmount nTotalIn,
        COrphan* porphan,
        std::vector<TxPriority>& vecPriority,
        const CTransaction* mempoolTx) const;
    void AddDependingTransactionsToPriorityQueue (
        std::map<uint256, std::vector<std::shared_ptr<COrphan>>>& mapDependers,
        const uint256& hash,
        std::vector<TxPriority>& vecPriority,
        TxPriorityCompare& comparer) const;

    bool IsFreeTransaction (
        const uint256& hash,
        const bool& fSortedByFee,
        const CFeeRate& feeRate,
        const uint64_t& nBlockSize,
        const unsigned int& nTxSize,
        const unsigned int& nBlockMinSize,
        const CTransaction& tx) const;

    void AddTransactionToBlock (
        const CTransaction& tx,
        CBlockTemplate& blocktemplate) const;

    std::vector<TxPriority> PrioritizeMempoolTransactions (
        const int& nHeight,
        std::map<uint256, std::vector<std::shared_ptr<COrphan>>>& mapDependers,
        CCoinsViewCache& view) const;

    void PrioritizeFeePastPrioritySize (
        std::vector<TxPriority>& vecPriority,
        bool& fSortedByFee,
        TxPriorityCompare& comparer,
        const uint64_t& nBlockSize,
        const unsigned int& nTxSize,
        const unsigned int& nBlockPrioritySize,
        double& dPriority) const;
    std::vector<PrioritizedTransactionData> PrioritizeTransactions(
        std::vector<TxPriority>& vecPriority,
        const int& nHeight,
        CCoinsViewCache& view,
        std::map<uint256, std::vector<std::shared_ptr<COrphan>>>& mapDependers) const;
    void AddTransactionsToBlockIfPossible (
        const int& nHeight,
        CCoinsViewCache& view,
        CBlockTemplate& blocktemplate) const;
public:
    BlockMemoryPoolTransactionCollector(
        const CChain& activeChain,
        CTxMemPool& mempool,
        AnnotatedMixin<boost::recursive_mutex>& mainCS,
        const CFeeRate& txFeeRate);
    bool CollectTransactionsIntoBlock (
        CBlockTemplate& pblocktemplate) const override;
};

#endif // BLOCK_MEMORY_POOL_TRANSACTION_COLLECTOR_H
