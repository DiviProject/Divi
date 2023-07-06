#ifndef WALLET_TRANSACTION_RECORD_H
#define WALLET_TRANSACTION_RECORD_H
#include <I_AppendOnlyTransactionRecord.h>
class CCriticalSection;

class WalletTransactionRecord final: public I_AppendOnlyTransactionRecord
{
protected:
    CCriticalSection& cs_walletTxRecord;

    /** Map from the bare txid of transactions in the wallet to the matching
     *  transactions themselves.  */
    std::map<uint256, const CWalletTx*> mapBareTxid;
    std::map<uint256, CWalletTx> mapWallet;

public:
    WalletTransactionRecord(CCriticalSection& requiredWalletLock);

    /** Tries to look up a transaction in the wallet, either by hash (txid) or
     *  the bare txid that is used after segwit-light to identify outputs.  */
    const CWalletTx* GetWalletTx(const uint256& hash) const override;
    const TransactionsByHash& GetWalletTransactions() const override;
    std::pair<TransactionsByHash::iterator, bool> AddTransaction(const CWalletTx& newlyAddedTransaction) override;
    unsigned size() const override;
};

class PrunedWalletTransactionRecord final: public I_AppendOnlyTransactionRecord
{
private:
    const unsigned txCountOffset_;
    WalletTransactionRecord walletRecord_;
public:
    PrunedWalletTransactionRecord(
        CCriticalSection& requiredWalletLock,
        const unsigned txCountOffset);
    const CWalletTx* GetWalletTx(const uint256& hash) const override;
    const TransactionsByHash& GetWalletTransactions() const override;
    std::pair<TransactionsByHash::iterator, bool> AddTransaction(const CWalletTx& newlyAddedTransaction) override;
    unsigned size() const override;
};

#endif// WALLET_TRANSACTION_RECORD_H
