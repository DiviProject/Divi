#ifndef I_APPEND_ONLY_TRANSACTION_RECORD_H
#define I_APPEND_ONLY_TRANSACTION_RECORD_H
#include <uint256.h>
#include <WalletTx.h>
#include <map>
class uint256;
class CWalletTx;
class I_AppendOnlyTransactionRecord
{
public:
    typedef std::map<uint256, CWalletTx> TransactionsByHash;
    virtual ~I_AppendOnlyTransactionRecord() {}
    virtual const CWalletTx* GetWalletTx(const uint256& hash) const = 0;
    virtual const TransactionsByHash& GetWalletTransactions() const = 0;
    virtual std::pair<TransactionsByHash::iterator, bool> AddTransaction(const CWalletTx& newlyAddedTransaction) = 0;
    virtual unsigned size() const = 0;
};

#endif// I_APPEND_ONLY_TRANSACTION_RECORD_H