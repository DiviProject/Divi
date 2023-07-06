#include <WalletBalanceCalculator.h>

#include <FilteredTransactionsCalculator.h>
#include <WalletTx.h>
#include <I_SpentOutputTracker.h>
#include <I_UtxoOwnershipDetector.h>

WalletBalanceCalculator::WalletBalanceCalculator(
    const I_UtxoOwnershipDetector& utxoOwnershipDetector,
    const I_TransactionDetailCalculator<CAmount>& utxoBalanceCalculator,
    const I_AppendOnlyTransactionRecord& txRecord,
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator
    ): utxoOwnershipDetector_(utxoOwnershipDetector)
    , utxoBalanceCalculator_(utxoBalanceCalculator)
    , txRecord_(txRecord)
    , confsCalculator_(confsCalculator)
    , filteredTxCalculator_(
        txRecord_,
        confsCalculator_,
        utxoBalanceCalculator_)
{
}

CAmount WalletBalanceCalculator::getBalance(UtxoOwnershipFilter ownershipFilter) const
{
    CAmount totalBalance = 0;
    filteredTxCalculator_.applyCalculationToMatchingTransactions(TxFlag::CONFIRMED_AND_MATURE,ownershipFilter,totalBalance);
    return totalBalance;
}

CAmount WalletBalanceCalculator::getUnconfirmedBalance(UtxoOwnershipFilter ownershipFilter) const
{
    CAmount totalBalance = 0;
    filteredTxCalculator_.applyCalculationToMatchingTransactions(TxFlag::UNCONFIRMED,ownershipFilter,totalBalance);
    return totalBalance;
}

CAmount WalletBalanceCalculator::getImmatureBalance(UtxoOwnershipFilter ownershipFilter) const
{
    CAmount totalBalance = 0;
    filteredTxCalculator_.applyCalculationToMatchingTransactions(TxFlag::CONFIRMED_AND_IMMATURE,ownershipFilter,totalBalance);
    return totalBalance;
}
class FullyOwnedTransactionDetailCalculator final: public I_TransactionDetailCalculator<CAmount>
{
private:
    const I_AppendOnlyTransactionRecord& txRecord_;
    const I_UtxoOwnershipDetector& utxoOwnershipDetector_;
    const I_TransactionDetailCalculator<CAmount>& decorated_;

    bool allInputsAreSpendableByMe(const UtxoOwnershipFilter& ownershipFilter, const CWalletTx& transaction) const
    {
        const auto& walletTransactionsByHash = txRecord_.GetWalletTransactions();
        for(const CTxIn& input: transaction.vin)
        {
            const auto it = walletTransactionsByHash.find(input.prevout.hash);
            if(it == walletTransactionsByHash.end()) return false;
            if(!ownershipFilter.hasRequested(utxoOwnershipDetector_.isMine(it->second.vout[input.prevout.n]))) return false;
        }
        return true;
    }

public:
    FullyOwnedTransactionDetailCalculator(
        const I_AppendOnlyTransactionRecord& txRecord,
        const I_UtxoOwnershipDetector& utxoOwnershipDetector,
        const I_TransactionDetailCalculator<CAmount>& decorated
        ): txRecord_(txRecord)
        , utxoOwnershipDetector_(utxoOwnershipDetector)
        , decorated_(decorated)
    {
    }
    void calculate(const CWalletTx& transaction, const UtxoOwnershipFilter& ownershipFilter, CAmount& intermediateResult) const override
    {
        if(!allInputsAreSpendableByMe(ownershipFilter, transaction)) return;
        decorated_.calculate(transaction,ownershipFilter,intermediateResult);
    }
};

 CAmount WalletBalanceCalculator::getSpendableBalance(UtxoOwnershipFilter ownershipFilter) const
 {
    CAmount totalBalance = getBalance(ownershipFilter);
    FullyOwnedTransactionDetailCalculator fullyOwnedUtxoBalanceCalculator(txRecord_, utxoOwnershipDetector_, utxoBalanceCalculator_);
    FilteredTransactionsCalculator<CAmount> calculator(txRecord_, confsCalculator_, fullyOwnedUtxoBalanceCalculator);
    calculator.applyCalculationToMatchingTransactions(TxFlag::UNCONFIRMED,ownershipFilter,totalBalance);
    return totalBalance;
 }