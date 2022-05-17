#include <UtxoBalanceCalculator.h>

#include <WalletTx.h>
#include <IsMineType.h>
#include <I_SpentOutputTracker.h>
#include <I_UtxoOwnershipDetector.h>

UtxoBalanceCalculator::UtxoBalanceCalculator(
    const I_UtxoOwnershipDetector& ownershipDetector,
    const I_SpentOutputTracker& spentOutputTracker
    ): ownershipDetector_(ownershipDetector)
    , spentOutputTracker_(spentOutputTracker)
{
}

void UtxoBalanceCalculator::calculate(
    const CWalletTx& walletTransaction,
    const int txDepth,
    const UtxoOwnershipFilter& ownershipFilter,
    CAmount& intermediateBalance) const
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