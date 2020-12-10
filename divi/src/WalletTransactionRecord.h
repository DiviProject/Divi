#ifndef WALLET_TRANSACTION_RECORD_H
#define WALLET_TRANSACTION_RECORD_H
#include <sync.h>
#include <uint256.h>
#include <WalletTx.h>
#include <map>
struct WalletTransactionRecord
{
private:
    CCriticalSection& cs_walletTxRecord;
    const std::string walletFilename_;
    const bool databaseWritesAreDisallowed_;

    /** Map from the bare txid of transactions in the wallet to the matching
     *  transactions themselves.  */
    std::map<uint256, const CWalletTx*> mapBareTxid;

public:
    std::map<uint256, CWalletTx> mapWallet;

    WalletTransactionRecord(CCriticalSection& requiredWalletLock,const std::string& walletFilename);
    WalletTransactionRecord(CCriticalSection& requiredWalletLock);
    const CWalletTx* GetWalletTx(const uint256& hash) const;

    /** Tries to look up a transaction in the wallet, either by hash (txid) or
     *  the bare txid that is used after segwit-light to identify outputs.  */
    std::vector<const CWalletTx*> GetWalletTransactionReferences() const;
    std::pair<std::map<uint256, CWalletTx>::iterator, bool> AddTransaction(const CWalletTx& newlyAddedTransaction);
    void UpdateMetadata(const uint256& hashOfTransactionToUpdate, const CWalletTx& updatedTransaction, bool updateDiskAndTimestamp,bool writeToWalletDb=false);
};

#endif// WALLET_TRANSACTION_RECORD_H
