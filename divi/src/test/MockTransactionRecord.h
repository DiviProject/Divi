#ifndef MOCK_TRANSACTION_RECORD_H
#define MOCK_TRANSACTION_RECORD_H
#include <gmock/gmock.h>
#include <I_AppendOnlyTransactionRecord.h>

class MockTransactionRecord: public I_AppendOnlyTransactionRecord
{
public:
    typedef std::map<uint256, CWalletTx> TransactionsByHash;
    typedef std::pair<TransactionsByHash::iterator, bool> TransactionInsertionResult;
    MOCK_CONST_METHOD1(GetWalletTx, const CWalletTx*(const uint256& hash));
    MOCK_CONST_METHOD0(GetWalletTransactions, const TransactionsByHash&());
    MOCK_METHOD1(AddTransaction, TransactionInsertionResult(const CWalletTx& newlyAddedTransaction));
    MOCK_CONST_METHOD0(size,unsigned());
};
#endif// MOCK_TRANSACTION_RECORD_H
