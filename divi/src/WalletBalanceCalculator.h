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

class WalletBalanceCalculator: protected FilteredTransactionsCalculator<CAmount>
{
private:
    const I_UtxoOwnershipDetector& ownershipDetector_;
    const I_SpentOutputTracker& spentOutputTracker_;

    void calculate(const CWalletTx& walletTransaction, const int txDepth, const UtxoOwnershipFilter& ownershipFilter,CAmount& intermediateBalance) const override;
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