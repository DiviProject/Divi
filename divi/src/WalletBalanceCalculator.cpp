#include <WalletBalanceCalculator.h>

WalletBalanceCalculator::WalletBalanceCalculator(
    const I_AppendOnlyTransactionRecord& txRecord,
    const SpentOutputTracker& spentOutputTracker,
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator
    ): txRecord_(txRecord)
    , spentOutputTracker_(spentOutputTracker)
    , confsCalculator_(confsCalculator)
{
}

WalletBalanceCalculator::~WalletBalanceCalculator()
{
}