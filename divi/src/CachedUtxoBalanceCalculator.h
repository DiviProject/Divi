#ifndef CACHED_UTXO_BALANCE_CALCULATOR_H
#define CACHED_UTXO_BALANCE_CALCULATOR_H
#include <uint256.h>
#include <map>
#include <I_TransactionDetailCalculator.h>
#include <amount.h>

class CachedUtxoBalanceCalculator final: public I_CachedTransactionDetailCalculator<CAmount>
{
private:
    const I_TransactionDetailCalculator<CAmount>& utxoBalanceCalculator_;
    mutable std::map<uint256, std::map<uint8_t, CAmount>> balanceCache_;

public:
    CachedUtxoBalanceCalculator(const I_TransactionDetailCalculator<CAmount>& utxoBalanceCalculator);
    void calculate(
        const CWalletTx& walletTransaction,
        const UtxoOwnershipFilter& ownershipFilter,
        CAmount& intermediateBalance) const override;

    void recomputeCachedTxEntries(const CWalletTx& walletTransaction) const override;
};
#endif// CACHED_UTXO_BALANCE_CALCULATOR_H