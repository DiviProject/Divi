#ifndef MOCK_SPENT_OUTPUT_TRACKER_H
#define MOCK_SPENT_OUTPUT_TRACKER_H
#include <gmock/gmock.h>
#include <I_SpentOutputTracker.h>

class MockSpentOutputTracker: public I_SpentOutputTracker
{
public:
    typedef std::pair<CWalletTx*,bool> UpdateResult;
    typedef std::set<uint256> TxidSet;
    MOCK_METHOD2(UpdateSpends, UpdateResult(const CWalletTx& newlyAddedTransaction, bool loadedFromDisk));
    MOCK_CONST_METHOD3(IsSpent, bool(const uint256& hash, unsigned int n, const int minimumConfirmation));
    MOCK_CONST_METHOD1(GetConflictingTxHashes, TxidSet(const CWalletTx& tx));
};
#endif// MOCK_SPENT_OUTPUT_TRACKER_H
