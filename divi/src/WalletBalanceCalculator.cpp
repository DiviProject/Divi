#include <WalletBalanceCalculator.h>

#include <FilteredTransactionsCalculator.h>
#include <WalletTx.h>
#include <I_SpentOutputTracker.h>
#include <I_UtxoOwnershipDetector.h>
#include <UtxoBalanceCalculator.h>

CachedUtxoBalanceCalculator::CachedUtxoBalanceCalculator(
    const I_TransactionDetailCalculator<CAmount>& utxoBalance
    ): utxoBalance_(utxoBalance)
    , balanceCache_()
{
}

void CachedUtxoBalanceCalculator::calculate(
    const CWalletTx& walletTransaction,
    const int txDepth,
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
        utxoBalance_.calculate(walletTransaction,txDepth,ownershipFilter,balanceFromOutputs);
        balanceCache_[txid][ownershipFilter.underlyingBitMask()] = balanceFromOutputs;
        intermediateBalance += balanceFromOutputs;
    }
}

void CachedUtxoBalanceCalculator::recomputeCacheEntry(const CWalletTx& walletTransaction) const
{
    balanceCache_.erase(walletTransaction.GetHash());
}

WalletBalanceCalculator::WalletBalanceCalculator(
    const I_TransactionDetailCalculator<CAmount>& utxoBalanceCalculator,
    const I_AppendOnlyTransactionRecord& txRecord,
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator
    ): filteredTxCalculator_(
        txRecord,
        confsCalculator,
        utxoBalanceCalculator)
{
}

CAmount WalletBalanceCalculator::getBalance(UtxoOwnershipFilter ownershipFilter) const
{
    CAmount totalBalance = 0;
    filteredTxCalculator_.applyCalculationToMatchingTransactions(TxFlag::CONFIRMED_AND_MATURE,ownershipFilter,totalBalance);
    return totalBalance;
}

CAmount WalletBalanceCalculator::getUnconfirmedBalance(UtxoOwnershipFilter ownershipFilter) const
{
    CAmount totalBalance = 0;
    filteredTxCalculator_.applyCalculationToMatchingTransactions(TxFlag::UNCONFIRMED,ownershipFilter,totalBalance);
    return totalBalance;
}

CAmount WalletBalanceCalculator::getImmatureBalance(UtxoOwnershipFilter ownershipFilter) const
{
    CAmount totalBalance = 0;
    filteredTxCalculator_.applyCalculationToMatchingTransactions(TxFlag::CONFIRMED_AND_IMMATURE,ownershipFilter,totalBalance);
    return totalBalance;
}