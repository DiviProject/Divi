#ifndef WALLET_BALANCE_CALCULATOR_H
#define WALLET_BALANCE_CALCULATOR_H
#include <amount.h>
#include <IsMineType.h>
#include <FilteredTransactionsCalculator.h>

class I_AppendOnlyTransactionRecord;
class I_SpentOutputTracker;
class I_MerkleTxConfirmationNumberCalculator;
class I_UtxoOwnershipDetector;
class CWalletTx;

class I_WalletBalanceCalculator
{
public:
    virtual ~I_WalletBalanceCalculator(){}
    virtual CAmount getBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const = 0;
    virtual CAmount getUnconfirmedBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const = 0;
    virtual CAmount getImmatureBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const = 0;
};

class WalletBalanceCalculator final: protected I_TransactionDetailCalculator<CAmount>, public I_WalletBalanceCalculator
{
private:
    const I_UtxoOwnershipDetector& ownershipDetector_;
    const I_SpentOutputTracker& spentOutputTracker_;
    FilteredTransactionsCalculator<CAmount> filteredTxCalculator_;

    void calculate(const CWalletTx& walletTransaction, const int txDepth, const UtxoOwnershipFilter& ownershipFilter,CAmount& intermediateBalance) const override;
public:
    WalletBalanceCalculator(
        const I_UtxoOwnershipDetector& ownershipDetector,
        const I_AppendOnlyTransactionRecord& txRecord,
        const I_SpentOutputTracker& spentOutputTracker,
        const I_MerkleTxConfirmationNumberCalculator& confsCalculator);
    virtual ~WalletBalanceCalculator() = default;
    CAmount getBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const override;
    CAmount getUnconfirmedBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const override;
    CAmount getImmatureBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const override;
};
#endif// WALLET_BALANCE_CALCULATOR_H