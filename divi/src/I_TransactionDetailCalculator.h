#ifndef I_TRANSACTION_DETAIL_CALCULATOR_H
#define I_TRANSACTION_DETAIL_CALCULATOR_H
class UtxoOwnershipFilter;
class CWalletTx;

template<typename CalculationResult>
class I_TransactionDetailCalculator
{
public:
    virtual ~I_TransactionDetailCalculator(){}
    virtual void calculate(const CWalletTx& transaction, const UtxoOwnershipFilter& ownershipFilter, CalculationResult& intermediateResult) const = 0;
};

template<typename CalculationResult>
class I_CachedTransactionDetailCalculator: public I_TransactionDetailCalculator<CalculationResult>
{
public:
    virtual ~I_CachedTransactionDetailCalculator(){}
    virtual void recomputeCachedTxEntries(const CWalletTx& transaction) const = 0;
};
#endif//I_TRANSACTION_DETAIL_CALCULATOR_H