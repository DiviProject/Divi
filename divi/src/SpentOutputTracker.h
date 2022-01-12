#ifndef SPENT_OUTPUT_TRACKER_H
#define SPENT_OUTPUT_TRACKER_H
#include <map>
#include <I_SpentOutputTracker.h>

class I_AppendOnlyTransactionRecord;
class COutPoint;
class uint256;
class CWalletTx;
class I_MerkleTxConfirmationNumberCalculator;

class SpentOutputTracker final: public I_SpentOutputTracker
{
private:
    I_AppendOnlyTransactionRecord& transactionRecord_;
    const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator_;

    using TxSpends = std::multimap<COutPoint, uint256>;
    TxSpends mapTxSpends;

    void AddToSpends(const COutPoint& outpoint, const uint256& wtxid);
    void AddToSpends(const CWalletTx& tx);

public:
    SpentOutputTracker(
        I_AppendOnlyTransactionRecord& transactionRecord,
        const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator);
    /**
     * Used to keep track of spent outpoints, and
     * detect and report conflicts (double-spends or
     * mutated transactions where the mutant gets mined).
     */
    std::pair<CWalletTx*,bool> UpdateSpends(const CWalletTx& newlyAddedTransaction, bool loadedFromDisk) override;
    bool IsSpent(const uint256& hash, unsigned int n, const int minimumConfirmations) const override;
    std::set<uint256> GetConflictingTxHashes(const CWalletTx& tx) const override;
};
#endif// SPENT_OUTPUT_TRACKER_H
