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
    const std::string& walletFilename_;
public:
    std::map<uint256, CWalletTx> mapWallet;

    WalletTransactionRecord(CCriticalSection& requiredWalletLock,const std::string& walletFilename);
    const CWalletTx* GetWalletTx(const uint256& hash) const;
    std::vector<const CWalletTx*> GetWalletTransactionReferences() const;
    std::pair<std::map<uint256, CWalletTx>::iterator, bool> AddTransaction(uint256 hash, const CWalletTx& newlyAddedTransaction);
    void UpdateMetadata(const uint256& hashOfTransactionToUpdate, const CWalletTx& updatedTransaction, bool updateDiskAndTimestamp,bool writeToWalletDb=false);
};

#endif// WALLET_TRANSACTION_RECORD_H