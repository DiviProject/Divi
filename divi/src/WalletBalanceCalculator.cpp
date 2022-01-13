#include <WalletBalanceCalculator.h>

#include <I_AppendOnlyTransactionRecord.h>
WalletBalanceCalculator::WalletBalanceCalculator(
    const I_UtxoOwnershipDetector& ownershipDetector,
    const I_AppendOnlyTransactionRecord& txRecord,
    const I_SpentOutputTracker& spentOutputTracker,
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator
    ): ownershipDetector_(ownershipDetector)
    , txRecord_(txRecord)
    , spentOutputTracker_(spentOutputTracker)
    , confsCalculator_(confsCalculator)
{
}

WalletBalanceCalculator::~WalletBalanceCalculator()
{
}

CAmount WalletBalanceCalculator::getBalance() const
{
    const auto& transactionsByHash = txRecord_.GetWalletTransactions();
    return CAmount(0);
}