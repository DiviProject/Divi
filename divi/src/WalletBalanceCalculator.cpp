#include <WalletBalanceCalculator.h>

WalletBalanceCalculator::WalletBalanceCalculator(
    const I_AppendOnlyTransactionRecord& txRecord,
    const I_SpentOutputTracker& spentOutputTracker,
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator
    ): txRecord_(txRecord)
    , spentOutputTracker_(spentOutputTracker)
    , confsCalculator_(confsCalculator)
{
}

WalletBalanceCalculator::~WalletBalanceCalculator()
{
}

CAmount WalletBalanceCalculator::getBalance() const
{
    return CAmount(0);
}