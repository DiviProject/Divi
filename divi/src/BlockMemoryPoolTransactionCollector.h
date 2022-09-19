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

class BlockMap;
class CCriticalSection;
class CTxMemPoolEntry;
class CTransaction;
class CTxIn;
class CBlock;
class CCoinsViewCache;
class CBlockIndex;
class CTxMemPool;
class CBlockTemplate;
class CBlockHeader;
class CFeeRate;
class Settings;

struct PrioritizedTransactionData
{
    const CTransaction* tx;
    unsigned int transactionSigOpCount;
    CAmount fee;
    PrioritizedTransactionData();
    PrioritizedTransactionData(
        const CTransaction& transaction,
        unsigned transactionSigOps,
        CAmount feePaid);
};

class COrphan;

// We want to sort transactions by priority and fee rate, so:
struct TxPriority
{
public:
    double _coinAgeOfInputsPerByte;
    CFeeRate _nominalFeeRate;
    const CTransaction* _transactionRef;
    CAmount _feePaid;
    size_t _transactionSize;
    TxPriority() = delete;
    TxPriority(
        double coinAgeOfInputsPerByte,
        CFeeRate nominalFeeRate,
        const CTransaction* transactionRef,
        CAmount feePaid,
        size_t transactionSize
        ): _coinAgeOfInputsPerByte(coinAgeOfInputsPerByte)
        , _nominalFeeRate(nominalFeeRate)
        , _transactionRef(transactionRef)
        , _feePaid(feePaid)
        , _transactionSize(transactionSize)
    {
    }
};
class TxPriorityCompare;
class CChain;

class BlockMemoryPoolTransactionCollector: public I_BlockTransactionCollector
{
private:
    using DependingTransactionsMap = std::map<uint256, std::vector<std::shared_ptr<COrphan>>>;

    const CCoinsViewCache& baseCoinsViewCache_;
    const CChain& activeChain_;
    const BlockMap& blockIndexMap_;
    CTxMemPool& mempool_;
    CCriticalSection& mainCS_;
    const CFeeRate& txFeeRate_;
    const unsigned blockMaxSize_;
    const unsigned blockPrioritySize_;
    const unsigned blockMinSize_;

private:
    void RecordOrphanTransaction(
        std::shared_ptr<COrphan>& porphan,
        const CTransaction& tx,
        const CTxIn& txin,
        DependingTransactionsMap& mapDependers) const;

    void ComputeTransactionPriority(
        const CTxMemPoolEntry& tx,
        const int nHeight,
        COrphan* porphan,
        std::vector<TxPriority>& vecPriority) const;
    void AddDependingTransactionsToPriorityQueue(
        DependingTransactionsMap& mapDependers,
        const uint256& hash,
        std::vector<TxPriority>& vecPriority,
        TxPriorityCompare& comparer) const;

    bool ShouldSkipCheapTransaction(
        const CFeeRate& feeRate,
        const uint64_t currentBlockSize,
        const unsigned int transactionSize) const;

    void AddTransactionToBlock(
        const CTransaction& tx,
        const CAmount feePaid,
        CBlock& block) const;

    std::vector<TxPriority> ComputeMempoolTransactionPriorities(
        const int& nHeight,
        DependingTransactionsMap& mapDependers,
        const CCoinsViewCache& view) const;

    bool ShouldSwitchToPriotizationByFee(
        const uint64_t& currentBlockSize,
        const unsigned int& transactionSize,
        const bool mustPayFees) const;
    std::vector<PrioritizedTransactionData> PrioritizeTransactionsByBlockSpaceUsage(
        std::vector<TxPriority>& vecPriority,
        const int& nHeight,
        CCoinsViewCache& view,
        DependingTransactionsMap& mapDependers) const;
    void AddTransactionsToBlockIfPossible(
        const int& nHeight,
        CCoinsViewCache& view,
        CBlock& block) const;
public:
    BlockMemoryPoolTransactionCollector(
        const Settings& settings,
        const CCoinsViewCache& baseCoinsViewCache,
        const CChain& activeChain,
        const BlockMap& blockIndexMap,
        CTxMemPool& mempool,
        CCriticalSection& mainCS,
        const CFeeRate& txFeeRate);
    bool CollectTransactionsIntoBlock(
        CBlockTemplate& pblocktemplate) const override;
};

#endif // BLOCK_MEMORY_POOL_TRANSACTION_COLLECTOR_H
