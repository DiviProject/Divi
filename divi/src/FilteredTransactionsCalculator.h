#ifndef FILTERED_TRANSACTION_CALCULATOR_H
#define FILTERED_TRANSACTION_CALCULATOR_H
class I_AppendOnlyTransactionRecord;
class I_MerkleTxConfirmationNumberCalculator;
class CWalletTx;
class UtxoOwnershipFilter;

template<typename CalculationResult>
class FilteredTransactionsCalculator
{
private:
    const I_AppendOnlyTransactionRecord& txRecord_;
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator_;
protected:
    virtual void calculate(const CWalletTx& transaction, const int txDepth, const UtxoOwnershipFilter& ownershipFilter, CalculationResult& intermediateResult) const = 0;
public:
    FilteredTransactionsCalculator(
        const I_AppendOnlyTransactionRecord& txRecord,
        const I_MerkleTxConfirmationNumberCalculator& confsCalculator);
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
#endif//FILTERED_TRANSACTION_CALCULATOR_H