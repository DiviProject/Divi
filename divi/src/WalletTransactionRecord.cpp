#include <WalletTransactionRecord.h>
#include <sync.h>

WalletTransactionRecord::WalletTransactionRecord(
    CCriticalSection& requiredWalletLock
    ): cs_walletTxRecord(requiredWalletLock)
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

const I_AppendOnlyTransactionRecord::TransactionsByHash& WalletTransactionRecord::GetWalletTransactions() const
{
    AssertLockHeld(cs_walletTxRecord);
    return mapWallet;
}

std::pair<I_AppendOnlyTransactionRecord::TransactionsByHash::iterator, bool> WalletTransactionRecord::AddTransaction(const CWalletTx& newlyAddedTransaction)
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
        const unsigned txCountOffset
        ): walletRecord_(requiredWalletLock)
        , txCountOffset_(txCountOffset)
{

}

const CWalletTx* PrunedWalletTransactionRecord::GetWalletTx(const uint256& hash) const
{
    return walletRecord_.GetWalletTx(hash);
}
const I_AppendOnlyTransactionRecord::TransactionsByHash& PrunedWalletTransactionRecord::GetWalletTransactions() const
{
    return walletRecord_.GetWalletTransactions();
}
std::pair<I_AppendOnlyTransactionRecord::TransactionsByHash::iterator, bool> PrunedWalletTransactionRecord::AddTransaction(const CWalletTx& newlyAddedTransaction)
{
    return walletRecord_.AddTransaction(newlyAddedTransaction);
}

unsigned PrunedWalletTransactionRecord::size() const
{
    return txCountOffset_ + walletRecord_.size();
}