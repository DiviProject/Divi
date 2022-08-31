#include <AvailableUtxoCollector.h>

#include <Settings.h>
#include <I_UtxoOwnershipDetector.h>
#include <I_AppendOnlyTransactionRecord.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>
#include <I_SpentOutputTracker.h>

AvailableUtxoCollector::AvailableUtxoCollector(
    const Settings& settings,
    const BlockMap& blockIndexByHash,
    const CChain& activeChain,
    const I_AppendOnlyTransactionRecord& txRecord,
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator,
    const I_UtxoOwnershipDetector& ownershipDetector,
    const I_SpentOutputTracker& spentOutputTracker,
    const LockedCoinsSet& lockedCoins,
    CCriticalSection& mainCriticalSection
    ): availableUtxoCalculator_(
        blockIndexByHash,
        activeChain,
        settings.GetArg("-vault_min",0)*COIN,
        txRecord,
        confsCalculator,
        ownershipDetector,
        spentOutputTracker,
        lockedCoins,
        mainCriticalSection)
    , filteredTransactions_(txRecord,confsCalculator,availableUtxoCalculator_)
{
}

void AvailableUtxoCollector::setCoinTypeAndGetAvailableUtxos(bool onlyConfirmed, AvailableCoinsType coinType, std::vector<COutput>& outputs) const
{
    availableUtxoCalculator_.setRequirements(coinType,onlyConfirmed,false);
    outputs.clear();
    filteredTransactions_.applyCalculationToMatchingTransactions(TxFlag::CONFIRMED_AND_MATURE,isminetype::ISMINE_SPENDABLE,outputs);
    if(!onlyConfirmed)
    {
        availableUtxoCalculator_.setRequirements(coinType,onlyConfirmed,true);
        filteredTransactions_.applyCalculationToMatchingTransactions(TxFlag::UNCONFIRMED,isminetype::ISMINE_SPENDABLE,outputs);
    }
}