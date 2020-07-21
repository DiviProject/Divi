#ifndef BLOCK_MEMORY_POOL_TRANSACTION_COLLECTOR_H
#define BLOCK_MEMORY_POOL_TRANSACTION_COLLECTOR_H

#include <amount.h>
#include <BlockTemplate.h>
#include <FeeRate.h>
#include <uint256.h>

#include <list>
#include <memory>
#include <set>
#include <stdint.h>
#include <vector>

#include <boost/tuple/tuple.hpp>
#include <boost/thread/recursive_mutex.hpp>


class CTransaction;
class CTxIn;
class CMutableTransaction;
class CWallet;
class CBlock;
class CCoinsViewCache;
class CBlockIndex;
class CTxMemPool;

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

class I_BlockTransactionCollector
{
public:
    virtual ~I_BlockTransactionCollector(){}
    virtual bool CollectTransactionsIntoBlock (
        std::unique_ptr<CBlockTemplate>& pblocktemplate,
        bool& fProofOfStake,
        CMutableTransaction& txNew) const = 0;
};

class BlockMemoryPoolTransactionCollector: public I_BlockTransactionCollector
{
private:
    CTxMemPool& mempool_;
    AnnotatedMixin<boost::recursive_mutex>& mainCS_;
private: 
    void UpdateTime(CBlockHeader* block, const CBlockIndex* pindexPrev) const;
    void SetBlockHeaders(CBlock& block, const bool& proofOfStake, const CBlockIndex& indexPrev, std::unique_ptr<CBlockTemplate>& pblocktemplate) const;
    bool VerifyUTXOIsKnownToMemPool (const CTxIn& txin, bool& fMissingInputs) const;
    bool CheckUTXOValidity (const CTxIn& txin, bool& fMissingInputs, const CTransaction &tx) const;
    void RecordOrphanTransaction (
        COrphan* porphan, 
        std::list<COrphan>& vOrphan, 
        const CTransaction& tx, 
        const CTxIn& txin,
        std::map<uint256, std::vector<COrphan*> >& mapDependers) const;

    void ComputeTransactionPriority (
        double& dPriority, 
        const CTransaction& tx, 
        CAmount nTotalIn, 
        COrphan* porphan, 
        std::vector<TxPriority>& vecPriority,
        const CTransaction* mempoolTx) const;
    void AddDependingTransactionsToPriorityQueue (
        std::map<uint256, std::vector<COrphan*> >& mapDependers,
        const uint256& hash,
        std::vector<TxPriority>& vecPriority,
        TxPriorityCompare& comparer) const;

    void SetCoinBaseTransaction (
        CBlock& block, 
        std::unique_ptr<CBlockTemplate>& pblocktemplate,
        const bool& fProofOfStake, 
        const int& nHeight,
        CMutableTransaction& txNew,
        const CAmount& nFees) const;
    
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
        std::unique_ptr<CBlockTemplate>& pblocktemplate) const;

    std::vector<TxPriority> PrioritizeMempoolTransactions (
        const int& nHeight,
        std::list<COrphan>& vOrphan,
        std::map<uint256, std::vector<COrphan*> >& mapDependers,
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
        std::map<uint256, std::vector<COrphan*> >& mapDependers) const;
    void AddTransactionsToBlockIfPossible (
        const int& nHeight,
        CCoinsViewCache& view,
        std::unique_ptr<CBlockTemplate>& pblocktemplate) const;
public:
    BlockMemoryPoolTransactionCollector(
        CTxMemPool& mempool, 
        AnnotatedMixin<boost::recursive_mutex>& mainCS);
    virtual bool CollectTransactionsIntoBlock (
        std::unique_ptr<CBlockTemplate>& pblocktemplate,
        bool& fProofOfStake,
        CMutableTransaction& txNew) const;
};

#endif // BLOCK_MEMORY_POOL_TRANSACTION_COLLECTOR_H
