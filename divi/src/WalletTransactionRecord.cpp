#include <WalletTransactionRecord.h>
#include <sync.h>

WalletTransactionRecord::WalletTransactionRecord(
    CCriticalSection& requiredWalletLock,
    const std::string& walletFilename
    ): cs_walletTxRecord(requiredWalletLock)
    , walletFilename_(walletFilename)
    , databaseWritesAreDisallowed_(false)
    , mapWallet()
{
}

WalletTransactionRecord::WalletTransactionRecord(
    CCriticalSection& requiredWalletLock
    ): cs_walletTxRecord(requiredWalletLock)
    , walletFilename_("")
    , databaseWritesAreDisallowed_(true)
    , mapWallet()
{

}

const CWalletTx* WalletTransactionRecord::GetWalletTx(const uint256& hash) const
{
    AssertLockHeld(cs_walletTxRecord);

    {
        const auto mit = mapWallet.find(hash);
        if (mit != mapWallet.end())
            return &mit->second;
    }

    {
        const auto mit = mapBareTxid.find(hash);
        if (mit != mapBareTxid.end())
            return mit->second;
    }

    return nullptr;
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

std::pair<std::map<uint256, CWalletTx>::iterator, bool> WalletTransactionRecord::AddTransaction(const CWalletTx& newlyAddedTransaction)
{
    AssertLockHeld(cs_walletTxRecord);

    auto res = mapWallet.emplace(newlyAddedTransaction.GetHash(), newlyAddedTransaction);
    if (res.second)
      mapBareTxid.emplace(newlyAddedTransaction.GetBareTxid(), &res.first->second);

    return res;
};

unsigned WalletTransactionRecord::size() const
{
    AssertLockHeld(cs_walletTxRecord);
    return mapWallet.size();
}

PrunedWalletTransactionRecord::PrunedWalletTransactionRecord(
        CCriticalSection& requiredWalletLock,
        const std::string& walletFilename,
        const unsigned txCountOffset
        ): WalletTransactionRecord(requiredWalletLock,walletFilename)
        , txCountOffset_(txCountOffset)
{

}
unsigned PrunedWalletTransactionRecord::size() const
{
    AssertLockHeld(cs_walletTxRecord);
    return txCountOffset_ + mapWallet.size();
}