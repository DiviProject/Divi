#ifndef SPENT_OUTPUT_TRACKER_H
#define SPENT_OUTPUT_TRACKER_H
#include <map>
#include <utility>
#include <set>
#include <uint256.h>

class WalletTransactionRecord;
class COutPoint;
class uint256;
class CWalletTx;

class SpentOutputTracker
{
private:
    WalletTransactionRecord& transactionRecord_;
protected:
    using TxSpends = std::multimap<COutPoint, uint256>;
    TxSpends mapTxSpends;
    void AddToSpends(const COutPoint& outpoint, const uint256& wtxid);
    void AddToSpends(const uint256& wtxid);
    void SyncMetaData(std::pair<TxSpends::iterator, TxSpends::iterator>);
public:
    SpentOutputTracker(WalletTransactionRecord& transactionRecord);
    /**
     * Used to keep track of spent outpoints, and
     * detect and report conflicts (double-spends or
     * mutated transactions where the mutant gets mined).
     */
    std::pair<CWalletTx*,bool> UpdateSpends(
        const CWalletTx& newlyAddedTransaction,
        int64_t orderedTransactionIndex=0,
        bool updateTransactionOrdering=false);
    bool IsSpent(const uint256& hash, unsigned int n) const;
    std::set<uint256> GetConflictingTxHashes(const CWalletTx& tx) const;
};
#endif// SPENT_OUTPUT_TRACKER_H