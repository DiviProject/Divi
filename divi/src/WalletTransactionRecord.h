#ifndef WALLET_TRANSACTION_RECORD_H
#define WALLET_TRANSACTION_RECORD_H
#include <uint256.h>
#include <WalletTx.h>
#include <map>
class CCriticalSection;
class WalletTransactionRecord
{
protected:
    CCriticalSection& cs_walletTxRecord;

    /** Map from the bare txid of transactions in the wallet to the matching
     *  transactions themselves.  */
    std::map<uint256, const CWalletTx*> mapBareTxid;

public:
    std::map<uint256, CWalletTx> mapWallet;

    WalletTransactionRecord(CCriticalSection& requiredWalletLock);
    virtual const CWalletTx* GetWalletTx(const uint256& hash) const;

    /** Tries to look up a transaction in the wallet, either by hash (txid) or
     *  the bare txid that is used after segwit-light to identify outputs.  */
    virtual std::vector<const CWalletTx*> GetWalletTransactionReferences() const;
    virtual std::pair<std::map<uint256, CWalletTx>::iterator, bool> AddTransaction(const CWalletTx& newlyAddedTransaction);
    virtual unsigned size() const;
};

class PrunedWalletTransactionRecord final: public WalletTransactionRecord
{
private:
    const unsigned txCountOffset_;
public:
    PrunedWalletTransactionRecord(
        CCriticalSection& requiredWalletLock,
        const unsigned txCountOffset);
    virtual unsigned size() const override;
};

#endif// WALLET_TRANSACTION_RECORD_H
