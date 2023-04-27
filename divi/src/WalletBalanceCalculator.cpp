#include <WalletBalanceCalculator.h>

#include <FilteredTransactionsCalculator.h>
#include <WalletTx.h>
#include <I_SpentOutputTracker.h>
#include <I_UtxoOwnershipDetector.h>

WalletBalanceCalculator::WalletBalanceCalculator(
    const I_TransactionDetailCalculator<CAmount>& utxoBalanceCalculator,
    const I_AppendOnlyTransactionRecord& txRecord,
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator
    ): utxoBalanceCalculator_(utxoBalanceCalculator)
    , txRecord_(txRecord)
    , confsCalculator_(confsCalculator)
    , filteredTxCalculator_(
        txRecord_,
        confsCalculator_,
        utxoBalanceCalculator_)
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

 CAmount WalletBalanceCalculator::getSpendableBalance(UtxoOwnershipFilter ownershipFilter) const
 {
    CAmount totalBalance = getBalance(ownershipFilter);
    FilteredTransactionsCalculator<CAmount> calculator(txRecord_, confsCalculator_, utxoBalanceCalculator_);
    calculator.applyCalculationToMatchingTransactions(TxFlag::UNCONFIRMED,ownershipFilter,totalBalance);
    return totalBalance;
 }