#include <CachedUtxoBalanceCalculator.h>

#include <WalletTx.h>
#include <IsMineType.h>

CachedUtxoBalanceCalculator::CachedUtxoBalanceCalculator(
    const I_TransactionDetailCalculator<CAmount>& utxoBalanceCalculator
    ): utxoBalanceCalculator_(utxoBalanceCalculator)
    , balanceCache_()
{
}

void CachedUtxoBalanceCalculator::calculate(
    const CWalletTx& walletTransaction,
    const UtxoOwnershipFilter& ownershipFilter,
    CAmount& intermediateBalance) const
{
    auto txid = walletTransaction.GetHash();
    if(balanceCache_.count(txid) > 0 && balanceCache_[txid].count(ownershipFilter.underlyingBitMask()))
    {
        intermediateBalance += balanceCache_[txid][ownershipFilter.underlyingBitMask()];
    }
    else
    {
        CAmount balanceFromOutputs = 0;
        utxoBalanceCalculator_.calculate(walletTransaction,ownershipFilter,balanceFromOutputs);
        balanceCache_[txid][ownershipFilter.underlyingBitMask()] = balanceFromOutputs;
        intermediateBalance += balanceFromOutputs;
    }
}

void CachedUtxoBalanceCalculator::recomputeCachedTxEntries(const CWalletTx& walletTransaction) const
{
    balanceCache_.erase(walletTransaction.GetHash());
}