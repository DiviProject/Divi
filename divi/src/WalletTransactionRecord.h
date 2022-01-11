#ifndef WALLET_TRANSACTION_RECORD_H
#define WALLET_TRANSACTION_RECORD_H
#include <uint256.h>
#include <WalletTx.h>
#include <map>
class CCriticalSection;

class I_AppendOnlyTransactionRecord
{
public:
    virtual ~I_AppendOnlyTransactionRecord() {}
    virtual const CWalletTx* GetWalletTx(const uint256& hash) const = 0;
    virtual const std::map<uint256, CWalletTx>& GetWalletTransactions() const = 0;
    virtual std::pair<std::map<uint256, CWalletTx>::iterator, bool> AddTransaction(const CWalletTx& newlyAddedTransaction) = 0;
    virtual unsigned size() const = 0;
};

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
    const std::map<uint256, CWalletTx>& GetWalletTransactions() const override;
    std::pair<std::map<uint256, CWalletTx>::iterator, bool> AddTransaction(const CWalletTx& newlyAddedTransaction) override;
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
    const std::map<uint256, CWalletTx>& GetWalletTransactions() const override;
    std::pair<std::map<uint256, CWalletTx>::iterator, bool> AddTransaction(const CWalletTx& newlyAddedTransaction) override;
    unsigned size() const override;
};

#endif// WALLET_TRANSACTION_RECORD_H
