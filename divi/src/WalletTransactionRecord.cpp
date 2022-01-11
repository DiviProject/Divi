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

const std::map<uint256, CWalletTx>& WalletTransactionRecord::GetWalletTransactions() const
{
    AssertLockHeld(cs_walletTxRecord);
    return mapWallet;
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
        const unsigned txCountOffset
        ): WalletTransactionRecord(requiredWalletLock)
        , txCountOffset_(txCountOffset)
{

}
unsigned PrunedWalletTransactionRecord::size() const
{
    AssertLockHeld(cs_walletTxRecord);
    return txCountOffset_ + mapWallet.size();
}