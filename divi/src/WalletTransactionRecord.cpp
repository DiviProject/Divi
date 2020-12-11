#include <WalletTransactionRecord.h>

#include <walletdb.h>

static bool WriteTxToDisk(const std::string& walletFilename, const CWalletTx& transactionToWrite)
{
    return CWalletDB(walletFilename).WriteTx(transactionToWrite.GetHash(),transactionToWrite);
}

WalletTransactionRecord::WalletTransactionRecord(
    CCriticalSection& requiredWalletLock,
    const std::string& walletFilename
    ): cs_walletTxRecord(requiredWalletLock)
    , walletFilename_(walletFilename)
    , mapWallet()
{
}

const CWalletTx* WalletTransactionRecord::GetWalletTx(const uint256& hash) const
{
    AssertLockHeld(cs_walletTxRecord);
    std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(hash);
    if (it == mapWallet.end())
        return NULL;
    return &(it->second);
}
std::vector<const CWalletTx*> WalletTransactionRecord::GetWalletTransactionReferences() const
{
    AssertLockHeld(cs_walletTxRecord);
    std::vector<const CWalletTx*> transactions;
    transactions.reserve(mapWallet.size());
    for (std::map<uint256, CWalletTx>::const_iterator it = mapWallet.cbegin(); it != mapWallet.cend(); ++it)
    {
        transactions.push_back( &(it->second) );
    }
    return transactions;
}

std::pair<std::map<uint256, CWalletTx>::iterator, bool> WalletTransactionRecord::AddTransaction(uint256 hash, const CWalletTx& newlyAddedTransaction)
{
    AssertLockHeld(cs_walletTxRecord);
    return  mapWallet.insert(std::make_pair(hash, newlyAddedTransaction));
};

void WalletTransactionRecord::UpdateMetadata(
    const uint256& hashOfTransactionToUpdate,
    const CWalletTx& updatedTransaction,
    bool updateDiskAndTimestamp,
    bool writeToWalletDb)
{
    AssertLockHeld(cs_walletTxRecord);
    CWalletTx* wtxToUpdate = const_cast<CWalletTx*>(GetWalletTx(hashOfTransactionToUpdate));
    if (wtxToUpdate != nullptr)
    {
        const CWalletTx* copyFrom = &updatedTransaction;

        wtxToUpdate->mapValue = copyFrom->mapValue;
        wtxToUpdate->vOrderForm = copyFrom->vOrderForm;
        wtxToUpdate->nTimeSmart = copyFrom->nTimeSmart;
        wtxToUpdate->createdByMe = copyFrom->createdByMe;
        wtxToUpdate->strFromAccount = copyFrom->strFromAccount;

        if(updateDiskAndTimestamp)
        {
            wtxToUpdate->nTimeReceived = copyFrom->nTimeReceived;
            wtxToUpdate->nOrderPos = copyFrom->nOrderPos;
            if(writeToWalletDb) WriteTxToDisk(walletFilename_,*wtxToUpdate);
        }
    }
}
