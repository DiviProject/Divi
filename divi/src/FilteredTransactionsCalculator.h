#ifndef FILTERED_TRANSACTION_CALCULATOR_H
#define FILTERED_TRANSACTION_CALCULATOR_H
#include <WalletTx.h>
#include <I_AppendOnlyTransactionRecord.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>
class UtxoOwnershipFilter;

template<typename CalculationResult>
class I_TransactionDetailCalculator
{
public:
    virtual ~I_TransactionDetailCalculator(){}
    virtual void calculate(const CWalletTx& transaction, const int txDepth, const UtxoOwnershipFilter& ownershipFilter, CalculationResult& intermediateResult) const = 0;
};

template<typename CalculationResult>
class FilteredTransactionsCalculator
{
private:
    const I_AppendOnlyTransactionRecord& txRecord_;
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator_;
    const I_TransactionDetailCalculator<CalculationResult>& txDetailCalculator_;

public:
    FilteredTransactionsCalculator(
        const I_AppendOnlyTransactionRecord& txRecord,
        const I_MerkleTxConfirmationNumberCalculator& confsCalculator,
        const I_TransactionDetailCalculator<CalculationResult>& txDetailCalculator);
    ~FilteredTransactionsCalculator();

    enum TxFlag
    {
        UNCONFIRMED = 1 << 0,
        CONFIRMED = 1 << 1,
        IMMATURE = 1 << 2,
        MATURE = 1 << 3,
        CONFIRMED_AND_MATURE = CONFIRMED | MATURE,
        CONFIRMED_AND_IMMATURE = CONFIRMED | IMMATURE,
    };
    void applyCalculationToMatchingTransactions(
        TxFlag flag,
        const UtxoOwnershipFilter& ownershipFilter,
        CalculationResult& initialValue) const;
};

template <typename CalculationResult>
FilteredTransactionsCalculator<CalculationResult>::FilteredTransactionsCalculator(
    const I_AppendOnlyTransactionRecord& txRecord,
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator,
    const I_TransactionDetailCalculator<CalculationResult>& txDetailCalculator
    ): txRecord_(txRecord)
    , confsCalculator_(confsCalculator)
    , txDetailCalculator_(txDetailCalculator)
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

        txDetailCalculator_.calculate(tx,depth, ownershipFilter,initialValue);
    }
}

#endif//FILTERED_TRANSACTION_CALCULATOR_H