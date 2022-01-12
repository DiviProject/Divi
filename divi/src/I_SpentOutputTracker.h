#ifndef I_SPENT_OUTPUT_TRACKER_H
#define I_SPENT_OUTPUT_TRACKER_H
#include <utility>
#include <set>
#include <uint256.h>
class CWalletTx;
class I_SpentOutputTracker
{
public:
    virtual ~I_SpentOutputTracker() {};
    virtual std::pair<CWalletTx*,bool> UpdateSpends(const CWalletTx& newlyAddedTransaction, bool loadedFromDisk) = 0;
    virtual bool IsSpent(const uint256& hash, unsigned int n, const int minimumConfirmations) const = 0;
    virtual std::set<uint256> GetConflictingTxHashes(const CWalletTx& tx) const = 0;
};
#endif// I_SPENT_OUTPUT_TRACKER_H