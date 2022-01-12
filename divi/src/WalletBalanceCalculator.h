#ifndef WALLET_BALANCE_CALCULATOR_H
#define WALLET_BALANCE_CALCULATOR_H
class I_AppendOnlyTransactionRecord;
class SpentOutputTracker;
class I_MerkleTxConfirmationNumberCalculator;
class WalletBalanceCalculator
{
    const I_AppendOnlyTransactionRecord& txRecord_;
    const SpentOutputTracker& spentOutputTracker_;
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator_;
public:
    WalletBalanceCalculator(
        const I_AppendOnlyTransactionRecord& txRecord,
        const SpentOutputTracker& spentOutputTracker,
        const I_MerkleTxConfirmationNumberCalculator& confsCalculator);
    ~WalletBalanceCalculator();
};
#endif// WALLET_BALANCE_CALCULATOR_H