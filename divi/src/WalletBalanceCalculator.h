#ifndef WALLET_BALANCE_CALCULATOR_H
#define WALLET_BALANCE_CALCULATOR_H
#include <amount.h>
#include <IsMineType.h>
#include <memory>
#include <FilteredTransactionsCalculator.h>
#include <uint256.h>

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

class CachedUtxoBalance final: public I_TransactionDetailCalculator<CAmount>
{
private:
    const I_TransactionDetailCalculator<CAmount>& utxoBalance_;
    mutable std::map<uint256, std::map<uint8_t, CAmount>> balanceCache_;

public:
    CachedUtxoBalance(const I_TransactionDetailCalculator<CAmount>& utxoBalance);
    void calculate(
        const CWalletTx& walletTransaction,
        const int txDepth,
        const UtxoOwnershipFilter& ownershipFilter,
        CAmount& intermediateBalance) const override;

    void recomputeCacheEntry(const CWalletTx& walletTransaction) const;
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