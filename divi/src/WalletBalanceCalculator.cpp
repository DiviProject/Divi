#include <WalletBalanceCalculator.h>

#include <FilteredTransactionsCalculator.h>
#include <WalletTx.h>
#include <I_SpentOutputTracker.h>
#include <I_UtxoOwnershipDetector.h>


WalletBalanceCalculator::WalletBalanceCalculator(
    const I_UtxoOwnershipDetector& ownershipDetector,
    const I_AppendOnlyTransactionRecord& txRecord,
    const I_SpentOutputTracker& spentOutputTracker,
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator
    ): ownershipDetector_(ownershipDetector)
    , spentOutputTracker_(spentOutputTracker)
    , filteredTxCalculator_(
        txRecord,
        confsCalculator,
        *static_cast<I_TransactionDetailCalculator<CAmount>*>(this))
{
}

void WalletBalanceCalculator::calculate(const CWalletTx& walletTransaction,const int txDepth, const UtxoOwnershipFilter& ownershipFilter,CAmount& intermediateBalance) const
{
    const uint256 txid = walletTransaction.GetHash();
    for(unsigned outputIndex=0u; outputIndex < walletTransaction.vout.size(); ++outputIndex)
    {
        if( ownershipFilter.hasRequested(ownershipDetector_.isMine(walletTransaction.vout[outputIndex])) &&
            !spentOutputTracker_.IsSpent(txid,outputIndex,0))
        {
            intermediateBalance += walletTransaction.vout[outputIndex].nValue;
        }
    }
}

CAmount WalletBalanceCalculator::getBalance(UtxoOwnershipFilter ownershipFilter) const
{
    CAmount totalBalance = 0;
    filteredTxCalculator_.applyCalculationToMatchingTransactions(FilteredTransactionsCalculator<CAmount>::CONFIRMED_AND_MATURE,ownershipFilter,totalBalance);
    return totalBalance;
}

CAmount WalletBalanceCalculator::getUnconfirmedBalance(UtxoOwnershipFilter ownershipFilter) const
{
    CAmount totalBalance = 0;
    filteredTxCalculator_.applyCalculationToMatchingTransactions(FilteredTransactionsCalculator<CAmount>::UNCONFIRMED,ownershipFilter,totalBalance);
    return totalBalance;
}

CAmount WalletBalanceCalculator::getImmatureBalance(UtxoOwnershipFilter ownershipFilter) const
{
    CAmount totalBalance = 0;
    filteredTxCalculator_.applyCalculationToMatchingTransactions(FilteredTransactionsCalculator<CAmount>::CONFIRMED_AND_IMMATURE,ownershipFilter,totalBalance);
    return totalBalance;
}