#include <FilteredTransactionsCalculator.h>

#include <WalletTx.h>
#include <I_AppendOnlyTransactionRecord.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>
#include <amount.h>
template class FilteredTransactionsCalculator<CAmount>;

template <typename CalculationResult>
FilteredTransactionsCalculator<CalculationResult>::FilteredTransactionsCalculator(
    const I_AppendOnlyTransactionRecord& txRecord,
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator
    ): txRecord_(txRecord)
    , confsCalculator_(confsCalculator)
{
}

template <typename CalculationResult>
FilteredTransactionsCalculator<CalculationResult>::~FilteredTransactionsCalculator()
{
}

template <typename CalculationResult>
void FilteredTransactionsCalculator<CalculationResult>::applyCalculationToMatchingTransactions(
    TxFlag flag,
    const UtxoOwnershipFilter& ownershipFilter,
    CalculationResult& initialValue) const
{
    assert((flag & TxFlag::UNCONFIRMED & TxFlag::CONFIRMED) == 0);
    assert((flag & TxFlag::IMMATURE & TxFlag::MATURE) == 0);
    // Only block rewards need non trivial maturity, and unconfirmed coin rewards are therefore conflicted
    assert((flag & TxFlag::UNCONFIRMED & TxFlag::IMMATURE) == 0);
    // Unconfirmed transactions that are not conflicted are mature.
    assert((flag & TxFlag::UNCONFIRMED & TxFlag::MATURE) == 0);
    for(const auto& txidAndTransaction: txRecord_.GetWalletTransactions())
    {
        const CWalletTx& tx = txidAndTransaction.second;
        const int depth = confsCalculator_.GetNumberOfBlockConfirmations(tx);
        if(depth < 0) continue;
        if(depth < 1 && (tx.IsCoinStake() || tx.IsCoinBase())) continue;
        if( (flag & TxFlag::UNCONFIRMED) > 0 && depth != 0) continue;
        if( (flag & TxFlag::CONFIRMED) > 0 && depth < 1) continue;
        if( (flag & TxFlag::MATURE) > 0 && confsCalculator_.GetBlocksToMaturity(tx) > 0) continue;
        if( (flag & TxFlag::IMMATURE) > 0 && confsCalculator_.GetBlocksToMaturity(tx) == 0) continue;

        calculate(tx,depth, ownershipFilter,initialValue);
    }
}
