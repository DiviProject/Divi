#include <AvailableUtxoCalculator.h>

#include <IsMineType.h>
#include <I_UtxoOwnershipDetector.h>
#include <I_AppendOnlyTransactionRecord.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>
#include <I_SpentOutputTracker.h>
#include <WalletTx.h>

#include <chain.h>
#include <blockmap.h>
#include <timedata.h>
#include <TransactionFinalityHelpers.h>

bool AvailableUtxoCalculator::IsAvailableType(const CTxOut& output, AvailableCoinsType coinType, isminetype& mine) const
{
    mine = ownershipDetector_.isMine(output);
    const bool isManagedVault = mine == isminetype::ISMINE_MANAGED_VAULT;
    const bool isOwnedVault = mine == isminetype::ISMINE_OWNED_VAULT;
    const bool isVault = isManagedVault || isOwnedVault;
    if(isVault) mine = isminetype::ISMINE_SPENDABLE;
    if( isManagedVault &&
        minimumVaultAmount_ > output.nValue &&
        coinType == AvailableCoinsType::STAKABLE_COINS)
    {
        return false;
    }
    if( coinType == AvailableCoinsType::STAKABLE_COINS && isOwnedVault)
    {
        return false;
    }
    else if(coinType == AvailableCoinsType::ALL_SPENDABLE_COINS && isVault)
    {
        return false;
    }
    else if( coinType == AvailableCoinsType::OWNED_VAULT_COINS && !isOwnedVault)
    {
        return false;
    }
    return true;
}

bool AvailableUtxoCalculator::allInputsAreSpendableByMe(const CWalletTx& walletTransaction, const UtxoOwnershipFilter& ownershipFilter) const
{
    const auto& walletTransactionsByHash = txRecord_.GetWalletTransactions();
    for(const CTxIn& input: walletTransaction.vin)
    {
        const auto it = walletTransactionsByHash.find(input.prevout.hash);
        if(it == walletTransactionsByHash.end()) return false;
        if(!ownershipFilter.hasRequested(ownershipDetector_.isMine(it->second.vout[input.prevout.n]))) return false;
    }
    return true;
}

AvailableUtxoCalculator::AvailableUtxoCalculator(
    const BlockMap& blockIndexByHash,
    const CChain& activeChain,
    const CAmount minimumVaultAmount,
    const I_AppendOnlyTransactionRecord& txRecord,
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator,
    const I_UtxoOwnershipDetector& ownershipDetector,
    const I_SpentOutputTracker& spentOutputTracker,
    const LockedCoinsSet& lockedCoins,
    CCriticalSection& mainCriticalSection
    ): txRecord_(txRecord)
    , ownershipDetector_(ownershipDetector)
    , minimumVaultAmount_(minimumVaultAmount)
    , blockIndexByHash_(blockIndexByHash)
    , activeChain_(activeChain)
    , spentOutputTracker_(spentOutputTracker)
    , lockedCoins_(lockedCoins)
    , coinType_(AvailableCoinsType::ALL_SPENDABLE_COINS)
    , onlyConfirmedTxs_(true)
    , requireInputsSpentByMe_(false)
    , mainCriticalSection_(mainCriticalSection)
{
}

void AvailableUtxoCalculator::calculate(
    const CWalletTx& walletTransaction,
    const UtxoOwnershipFilter& ownershipFilter,
    std::vector<COutput>& outputs) const
{
    if (!IsFinalTx(mainCriticalSection_,walletTransaction, activeChain_, activeChain_.Height() + (onlyConfirmedTxs_? 0:1), GetAdjustedTime())) return;
    if(!onlyConfirmedTxs_ && requireInputsSpentByMe_ && !allInputsAreSpendableByMe(walletTransaction,ownershipFilter)) return;
    const uint256& txid = walletTransaction.GetHash();
    for(unsigned outputIndex = 0u; outputIndex < walletTransaction.vout.size(); ++outputIndex)
    {
        isminetype mine;
        if(!IsAvailableType(walletTransaction.vout[outputIndex],coinType_,mine) || !ownershipFilter.hasRequested(mine)) continue;
        if(walletTransaction.vout[outputIndex].nValue <= 0 || lockedCoins_.count(COutPoint(txid,outputIndex)) > 0) continue;
        if(spentOutputTracker_.IsSpent(txid, outputIndex,0)) continue;

        BlockMap::const_iterator it = blockIndexByHash_.find(walletTransaction.hashBlock);
        const CBlockIndex* const blockIndexOfConfirmation = it == blockIndexByHash_.end()? nullptr: it->second;
        const int txDepth = (blockIndexOfConfirmation && activeChain_.Contains(blockIndexOfConfirmation))
            ? activeChain_.Height() - blockIndexOfConfirmation->nHeight + 1
            : 0;
        outputs.emplace_back(COutput(&walletTransaction, outputIndex, txDepth, true));
    }
}

void AvailableUtxoCalculator::setRequirements(const AvailableCoinsType coinType, const bool onlyConfirmedTxs, const bool requireInputsSpentByMe)
{
    coinType_ = coinType;
    onlyConfirmedTxs_ = onlyConfirmedTxs;
    requireInputsSpentByMe_ = requireInputsSpentByMe;
}