#include <WalletBalanceCalculator.h>

#include <WalletTx.h>
#include <I_AppendOnlyTransactionRecord.h>
#include <I_SpentOutputTracker.h>
#include <I_UtxoOwnershipDetector.h>

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
    CAmount totalBalance = 0;
    const auto& transactionsByHash = txRecord_.GetWalletTransactions();
    for(const auto& txidAndTransaction: transactionsByHash)
    {
        const uint256& txid = txidAndTransaction.first;
        const CWalletTx& tx = txidAndTransaction.second;
        for(unsigned outputIndex=0u; outputIndex < tx.vout.size(); ++outputIndex)
        {
            if(ownershipDetector_.isMine(tx.vout[outputIndex]) == isminetype::ISMINE_SPENDABLE &&
               !spentOutputTracker_.IsSpent(txid,outputIndex,0))
            {
                totalBalance += tx.vout[outputIndex].nValue;
            }
        }
    }
    return totalBalance;
}