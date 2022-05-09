#include <WalletBalanceCalculator.h>

#include <WalletTx.h>
#include <I_AppendOnlyTransactionRecord.h>
#include <I_SpentOutputTracker.h>
#include <I_UtxoOwnershipDetector.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>

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

CAmount WalletBalanceCalculator::calculateBalance(TxFlag flag, const UtxoOwnershipFilter& ownershipFilter) const
{
    assert((flag & TxFlag::UNCONFIRMED & TxFlag::CONFIRMED) == 0);
    assert((flag & TxFlag::IMMATURE & TxFlag::MATURE) == 0);
    // Only block rewards need non trivial maturity, and unconfirmed coin rewards are therefore conflicted
    assert((flag & TxFlag::UNCONFIRMED & TxFlag::IMMATURE) == 0);
    // Unconfirmed transactions that are not conflicted are mature.
    assert((flag & TxFlag::UNCONFIRMED & TxFlag::MATURE) == 0);
    CAmount totalBalance = 0;
    const auto& transactionsByHash = txRecord_.GetWalletTransactions();
    for(const auto& txidAndTransaction: transactionsByHash)
    {
        const CWalletTx& tx = txidAndTransaction.second;
        const int depth = confsCalculator_.GetNumberOfBlockConfirmations(tx);
        if(depth < 0) continue;
        if(depth < 1 && (tx.IsCoinStake() || tx.IsCoinBase())) continue;
        if( (flag & TxFlag::UNCONFIRMED) > 0 && depth != 0) continue;
        if( (flag & TxFlag::CONFIRMED) > 0 && depth < 1) continue;
        if( (flag & TxFlag::MATURE) > 0 && confsCalculator_.GetBlocksToMaturity(tx) > 0) continue;
        if( (flag & TxFlag::IMMATURE) > 0 && confsCalculator_.GetBlocksToMaturity(tx) == 0) continue;

        const uint256& txid = txidAndTransaction.first;
        for(unsigned outputIndex=0u; outputIndex < tx.vout.size(); ++outputIndex)
        {
            if( ownershipFilter.hasRequested(ownershipDetector_.isMine(tx.vout[outputIndex])) &&
               !spentOutputTracker_.IsSpent(txid,outputIndex,0))
            {
                totalBalance += tx.vout[outputIndex].nValue;
            }
        }
    }
    return totalBalance;
}

CAmount WalletBalanceCalculator::getBalance(UtxoOwnershipFilter ownershipFilter) const
{
    return calculateBalance(CONFIRMED_AND_MATURE,ownershipFilter);
}

CAmount WalletBalanceCalculator::getUnconfirmedBalance(UtxoOwnershipFilter ownershipFilter) const
{
    return calculateBalance(UNCONFIRMED,ownershipFilter);
}

CAmount WalletBalanceCalculator::getImmatureBalance(UtxoOwnershipFilter ownershipFilter) const
{
    return calculateBalance(CONFIRMED_AND_IMMATURE,ownershipFilter);
}