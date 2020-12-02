#include <SpentOutputTracker.h>

#include <primitives/transaction.h>
#include <WalletTx.h>
#include <WalletTransactionRecord.h>

SpentOutputTracker::SpentOutputTracker(
    WalletTransactionRecord& transactionRecord
    ): transactionRecord_(transactionRecord)
    , mapTxSpends()
{
}

void SpentOutputTracker::SyncMetaData(std::pair<TxSpends::iterator, TxSpends::iterator> range)
{
    // We want all the wallet transactions in range to have the same metadata as
    // the oldest (smallest nOrderPos).
    // So: find smallest nOrderPos:

    int nMinOrderPos = std::numeric_limits<int>::max();
    const CWalletTx* copyFrom = NULL;
    uint256 hashFrom(0);
    for (TxSpends::iterator it = range.first; it != range.second; ++it) {
        const CWalletTx* transactionPtr = transactionRecord_.GetWalletTx(it->second);
        if (transactionPtr) {
            int n = transactionPtr->nOrderPos;
            if(n < nMinOrderPos)
            {
                nMinOrderPos = n;
                copyFrom = transactionPtr;
                hashFrom = it->second;
            }
        }
    }
    // Now copy data from copyFrom to rest:
    for (TxSpends::iterator it = range.first; it != range.second; ++it)
    {
        const uint256& hash = it->second;
        if(hashFrom == hash) continue;
        transactionRecord_.UpdateMetadata(hash,*copyFrom,false);
    }
}

/**
 * Outpoint is spent if any non-conflicted transaction
 * spends it:
 */
bool SpentOutputTracker::IsSpent(const uint256& hash, unsigned int n) const
{
    const COutPoint outpoint(hash, n);
    std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;
    range = mapTxSpends.equal_range(outpoint);
    for (TxSpends::const_iterator it = range.first; it != range.second; ++it) {
        const uint256& wtxid = it->second;
        const CWalletTx* transactionPtr = transactionRecord_.GetWalletTx(wtxid);
        if (transactionPtr && transactionPtr->GetNumberOfBlockConfirmations() >= 0)
            return true; // Spent
    }
    return false;
}

std::pair<CWalletTx*,bool> SpentOutputTracker::UpdateSpends(
    const CWalletTx& newlyAddedTransaction,
    int64_t orderedTransactionIndex,
    bool updateTransactionOrdering)
{
    uint256 hash = newlyAddedTransaction.GetHash();
    std::pair<std::map<uint256, CWalletTx>::iterator, bool> ret = transactionRecord_.AddTransaction(hash, newlyAddedTransaction);
    if(ret.second)
    {
        if(updateTransactionOrdering) (*ret.first).second.nOrderPos = orderedTransactionIndex;
        AddToSpends(hash);
    }
    return std::make_pair(&(ret.first->second),ret.second);
}

void SpentOutputTracker::AddToSpends(const COutPoint& outpoint, const uint256& wtxid)
{
    mapTxSpends.insert(std::make_pair(outpoint, wtxid));
    std::pair<TxSpends::iterator, TxSpends::iterator> range;
    range = mapTxSpends.equal_range(outpoint);
    SyncMetaData(range);
}


void SpentOutputTracker::AddToSpends(const uint256& wtxid)
{
    const CWalletTx* transactionPtr = transactionRecord_.GetWalletTx(wtxid);
    assert(transactionPtr);
    if (transactionPtr->IsCoinBase()) // Coinbases don't spend anything!
        return;

    for(const CTxIn& txin: transactionPtr->vin)
    {
        AddToSpends(txin.prevout, wtxid);
    }
}

std::set<uint256> SpentOutputTracker::GetConflictingTxHashes(const CWalletTx& tx) const
{
    std::set<uint256> result;
    std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;

    for (const CTxIn& txin: tx.vin)
    {
        if (mapTxSpends.count(txin.prevout) <= 1 )
            continue; // No conflict if zero or one spends
        range = mapTxSpends.equal_range(txin.prevout);
        for (TxSpends::const_iterator it = range.first; it != range.second; ++it)
            result.insert(it->second);
    }
    return result;
}