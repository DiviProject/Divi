#ifndef WALLET_BALANCE_CALCULATOR_H
#define WALLET_BALANCE_CALCULATOR_H
#include <amount.h>
#include <IsMineType.h>
class I_AppendOnlyTransactionRecord;
class I_SpentOutputTracker;
class I_MerkleTxConfirmationNumberCalculator;
class I_UtxoOwnershipDetector;
class CWalletTx;

template<typename CalculationResult>
class FilteredTransactionsCalculator
{
private:
    const I_AppendOnlyTransactionRecord& txRecord_;
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator_;
protected:
    virtual void calculate(const CWalletTx& transaction, const UtxoOwnershipFilter& ownershipFilter, CalculationResult& intermediateResult) const = 0;
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

class WalletBalanceCalculator
{
private:
    const I_UtxoOwnershipDetector& ownershipDetector_;
    const I_AppendOnlyTransactionRecord& txRecord_;
    const I_SpentOutputTracker& spentOutputTracker_;
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator_;

    enum TxFlag
    {
        UNCONFIRMED = 1 << 0,
        CONFIRMED = 1 << 1,
        IMMATURE = 1 << 2,
        MATURE = 1 << 3,
        CONFIRMED_AND_MATURE = CONFIRMED | MATURE,
        CONFIRMED_AND_IMMATURE = CONFIRMED | IMMATURE,
    };
    CAmount calculateBalance(TxFlag flag,const UtxoOwnershipFilter& ownershipFilter) const;
public:
    WalletBalanceCalculator(
        const I_UtxoOwnershipDetector& ownershipDetector,
        const I_AppendOnlyTransactionRecord& txRecord,
        const I_SpentOutputTracker& spentOutputTracker,
        const I_MerkleTxConfirmationNumberCalculator& confsCalculator);
    ~WalletBalanceCalculator();
    CAmount getBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const;
    CAmount getUnconfirmedBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const;
    CAmount getImmatureBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const;
};
#endif// WALLET_BALANCE_CALCULATOR_H