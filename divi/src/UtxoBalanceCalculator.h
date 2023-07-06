#ifndef UTXO_BALANCE_CALCULATOR_H
#define UTXO_BALANCE_CALCULATOR_H
#include <I_TransactionDetailCalculator.h>
#include <amount.h>
class I_UtxoOwnershipDetector;
class I_SpentOutputTracker;
class UtxoBalanceCalculator final: public I_TransactionDetailCalculator<CAmount>
{
private:
    const I_UtxoOwnershipDetector& ownershipDetector_;
    const I_SpentOutputTracker& spentOutputTracker_;
public:
    UtxoBalanceCalculator(
        const I_UtxoOwnershipDetector& ownershipDetector,
        const I_SpentOutputTracker& spentOutputTracker);
    void calculate(
        const CWalletTx& walletTransaction,
        const UtxoOwnershipFilter& ownershipFilter,
        CAmount& intermediateBalance) const override;
};
#endif// UTXO_BALANCE_CALCULATOR_H