#ifndef WALLET_BALANCE_CALCULATOR_H
#define WALLET_BALANCE_CALCULATOR_H
#include <amount.h>
#include <IsMineType.h>
#include <memory>
#include <FilteredTransactionsCalculator.h>

class I_AppendOnlyTransactionRecord;
class I_SpentOutputTracker;
class I_MerkleTxConfirmationNumberCalculator;
class I_UtxoOwnershipDetector;
class CWalletTx;
class UtxoBalanceCalculator;

class I_WalletBalanceCalculator
{
public:
    virtual ~I_WalletBalanceCalculator(){}
    virtual CAmount getBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const = 0;
    virtual CAmount getUnconfirmedBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const = 0;
    virtual CAmount getImmatureBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const = 0;
};

class WalletBalanceCalculator final: public I_WalletBalanceCalculator
{
private:
    FilteredTransactionsCalculator<CAmount> filteredTxCalculator_;

public:
    WalletBalanceCalculator(
        const I_TransactionDetailCalculator<CAmount>& utxoBalanceCalculator,
        const I_AppendOnlyTransactionRecord& txRecord,
        const I_MerkleTxConfirmationNumberCalculator& confsCalculator);
    virtual ~WalletBalanceCalculator() = default;
    CAmount getBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const override;
    CAmount getUnconfirmedBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const override;
    CAmount getImmatureBalance(UtxoOwnershipFilter ownershipFilter = isminetype::ISMINE_SPENDABLE) const override;
};
#endif// WALLET_BALANCE_CALCULATOR_H