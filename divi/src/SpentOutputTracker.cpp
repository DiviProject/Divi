#include <SpentOutputTracker.h>

#include <primitives/transaction.h>
#include <WalletTx.h>
#include <WalletTransactionRecord.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>
#include <timedata.h>

SpentOutputTracker::SpentOutputTracker(
    I_AppendOnlyTransactionRecord& transactionRecord,
    const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator
    ): transactionRecord_(transactionRecord)
    , confirmationsCalculator_(confirmationsCalculator)
    , mapTxSpends()
{
}

/**
 * Outpoint is spent if any non-conflicted transaction
 * spends it:
 */
bool SpentOutputTracker::IsSpent(const uint256& hash, unsigned int n, const int minimumConfirmations) const
{
    const COutPoint outpoint(hash, n);
    std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;
    range = mapTxSpends.equal_range(outpoint);
    for (TxSpends::const_iterator it = range.first; it != range.second; ++it) {
        const uint256& wtxid = it->second;
        const CWalletTx* transactionPtr = transactionRecord_.GetWalletTx(wtxid);
        if (transactionPtr && confirmationsCalculator_.GetNumberOfBlockConfirmations(*transactionPtr) >= minimumConfirmations)
            return true; // Spent
    }
    return false;
}

std::pair<CWalletTx*,bool> SpentOutputTracker::UpdateSpends(
    const CWalletTx& newlyAddedTransaction,
    bool loadedFromDisk)
{
    std::pair<std::map<uint256, CWalletTx>::iterator, bool> ret = transactionRecord_.AddTransaction(newlyAddedTransaction);
    if(ret.second)
    {
        if(!loadedFromDisk)
        {
            CWalletTx& addedTx = (*ret.first).second;
            addedTx.nOrderPos = transactionRecord_.size();
            addedTx.nTimeReceived = GetAdjustedTime();
        }
        AddToSpends(ret.first->second);
    }
    return std::make_pair(&(ret.first->second),ret.second);
}

void SpentOutputTracker::AddToSpends(const COutPoint& outpoint, const uint256& wtxid)
{
    mapTxSpends.insert(std::make_pair(outpoint, wtxid));
}


void SpentOutputTracker::AddToSpends(const CWalletTx& tx)
{
    if (tx.IsCoinBase()) // Coinbases don't spend anything!
        return;

    const auto hash = tx.GetHash();
    for(const CTxIn& txin: tx.vin)
        AddToSpends(txin.prevout, hash);
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
