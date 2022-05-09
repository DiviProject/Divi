#include <WalletBalanceCalculator.h>

#include <WalletTx.h>
#include <I_AppendOnlyTransactionRecord.h>
#include <I_SpentOutputTracker.h>
#include <I_UtxoOwnershipDetector.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>

template <typename CalculationResult>
FilteredTransactionsCalculator<CalculationResult>::FilteredTransactionsCalculator(
    const I_AppendOnlyTransactionRecord& txRecord,
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator
    ): txRecord_(txRecord)
    , confsCalculator_(confsCalculator)
{
}

template <typename CalculationResult>
FilteredTransactionsCalculator<CalculationResult>::~FilteredTransactionsCalculator()
{
}

template <typename CalculationResult>
void FilteredTransactionsCalculator<CalculationResult>::applyCalculationToMatchingTransactions(
    TxFlag flag,
    const UtxoOwnershipFilter& ownershipFilter,
    CalculationResult& initialValue) const
{
    assert((flag & TxFlag::UNCONFIRMED & TxFlag::CONFIRMED) == 0);
    assert((flag & TxFlag::IMMATURE & TxFlag::MATURE) == 0);
    // Only block rewards need non trivial maturity, and unconfirmed coin rewards are therefore conflicted
    assert((flag & TxFlag::UNCONFIRMED & TxFlag::IMMATURE) == 0);
    // Unconfirmed transactions that are not conflicted are mature.
    assert((flag & TxFlag::UNCONFIRMED & TxFlag::MATURE) == 0);
    for(const auto& txidAndTransaction: txRecord_.GetWalletTransactions())
    {
        const CWalletTx& tx = txidAndTransaction.second;
        const int depth = confsCalculator_.GetNumberOfBlockConfirmations(tx);
        if(depth < 0) continue;
        if(depth < 1 && (tx.IsCoinStake() || tx.IsCoinBase())) continue;
        if( (flag & TxFlag::UNCONFIRMED) > 0 && depth != 0) continue;
        if( (flag & TxFlag::CONFIRMED) > 0 && depth < 1) continue;
        if( (flag & TxFlag::MATURE) > 0 && confsCalculator_.GetBlocksToMaturity(tx) > 0) continue;
        if( (flag & TxFlag::IMMATURE) > 0 && confsCalculator_.GetBlocksToMaturity(tx) == 0) continue;

        calculate(tx,ownershipFilter,initialValue);
    }
}



WalletBalanceCalculator::WalletBalanceCalculator(
    const I_UtxoOwnershipDetector& ownershipDetector,
    const I_AppendOnlyTransactionRecord& txRecord,
    const I_SpentOutputTracker& spentOutputTracker,
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator
    ): FilteredTransactionsCalculator<CAmount>(txRecord,confsCalculator)
    , ownershipDetector_(ownershipDetector)
    , spentOutputTracker_(spentOutputTracker)
{
}

WalletBalanceCalculator::~WalletBalanceCalculator()
{
}

void WalletBalanceCalculator::calculate(const CWalletTx& walletTransaction, const UtxoOwnershipFilter& ownershipFilter,CAmount& intermediateBalance) const
{
    const uint256 txid = walletTransaction.GetHash();
    for(unsigned outputIndex=0u; outputIndex < walletTransaction.vout.size(); ++outputIndex)
    {
        if( ownershipFilter.hasRequested(ownershipDetector_.isMine(walletTransaction.vout[outputIndex])) &&
            !spentOutputTracker_.IsSpent(txid,outputIndex,0))
        {
            intermediateBalance += walletTransaction.vout[outputIndex].nValue;
        }
    }
}

CAmount WalletBalanceCalculator::getBalance(UtxoOwnershipFilter ownershipFilter) const
{
    CAmount totalBalance = 0;
    applyCalculationToMatchingTransactions(CONFIRMED_AND_MATURE,ownershipFilter,totalBalance);
    return totalBalance;
}

CAmount WalletBalanceCalculator::getUnconfirmedBalance(UtxoOwnershipFilter ownershipFilter) const
{
    CAmount totalBalance = 0;
    applyCalculationToMatchingTransactions(UNCONFIRMED,ownershipFilter,totalBalance);
    return totalBalance;
}

CAmount WalletBalanceCalculator::getImmatureBalance(UtxoOwnershipFilter ownershipFilter) const
{
    CAmount totalBalance = 0;
    applyCalculationToMatchingTransactions(CONFIRMED_AND_IMMATURE,ownershipFilter,totalBalance);
    return totalBalance;
}