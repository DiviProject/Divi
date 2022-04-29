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

bool debitsFunds(const I_UtxoOwnershipDetector& ownershipDetector, const std::map<uint256, CWalletTx>& transactionsByHash,const CTransaction& tx)
{
    for(const auto& input: tx.vin)
    {
        auto it = transactionsByHash.find(input.prevout.hash);
        if(it != transactionsByHash.end())
        {
            const auto& outputsToBeSpent = it->second.vout;
            if(ownershipDetector.isMine(outputsToBeSpent[input.prevout.n]) == isminetype::ISMINE_SPENDABLE
                && outputsToBeSpent[input.prevout.n].nValue > 0)
            {
                return true;
            }
        }
    }
    return false;
}

CAmount WalletBalanceCalculator::calculateBalance(BalanceFlag flag) const
{
    assert((flag & BalanceFlag::UNCONFIRMED & BalanceFlag::CONFIRMED) == 0);
    CAmount totalBalance = 0;
    const auto& transactionsByHash = txRecord_.GetWalletTransactions();
    for(const auto& txidAndTransaction: transactionsByHash)
    {
        const CWalletTx& tx = txidAndTransaction.second;
        const int depth = confsCalculator_.GetNumberOfBlockConfirmations(tx);
        if(depth < 0) continue;
        if( (flag & BalanceFlag::UNCONFIRMED) > 0 && depth != 0) continue;
        if( (flag & BalanceFlag::CONFIRMED) > 0 && depth < 1) continue;
        if( (flag & BalanceFlag::TRUSTED) > 0)
        {
            if(depth==0 && !debitsFunds(ownershipDetector_,transactionsByHash,tx)) continue;
        }

        if(depth < 1 && (tx.IsCoinStake() || tx.IsCoinBase())) continue;
        if( confsCalculator_.GetBlocksToMaturity(tx) > 0) continue;

        const uint256& txid = txidAndTransaction.first;
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

CAmount WalletBalanceCalculator::getBalance() const
{
    return calculateBalance(TRUSTED);
}

CAmount WalletBalanceCalculator::getUnconfirmedBalance() const
{
    return calculateBalance(UNCONFIRMED);
}