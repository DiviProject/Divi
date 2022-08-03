#ifndef AVAILABLE_UTXO_COLLECTOR_H
#define AVAILABLE_UTXO_COLLECTOR_H
#include <vector>
#include <AvailableUtxoCalculator.h>
#include <Output.h>
#include <FilteredTransactionsCalculator.h>
#include <LockedCoinsSet.h>
#include <AvailableCoinsType.h>

class Settings;
class BlockMap;
class CChain;
class I_AppendOnlyTransactionRecord;
class I_MerkleTxConfirmationNumberCalculator;
class I_UtxoOwnershipDetector;
class I_SpentOutputTracker;

class AvailableUtxoCollector
{
private:
    mutable AvailableUtxoCalculator availableUtxoCalculator_;
    FilteredTransactionsCalculator<std::vector<COutput>> filteredTransactions_;

public:
    AvailableUtxoCollector(
        const Settings& settings,
        const BlockMap& blockIndexByHash,
        const CChain& activeChain,
        const I_AppendOnlyTransactionRecord& txRecord,
        const I_MerkleTxConfirmationNumberCalculator& confsCalculator,
        const I_UtxoOwnershipDetector& ownershipDetector,
        const I_SpentOutputTracker& spentOutputTracker,
        const LockedCoinsSet& lockedCoins);
    ~AvailableUtxoCollector() = default;

    void setCoinTypeAndGetAvailableUtxos(bool onlyConfirmed, AvailableCoinsType coinType, std::vector<COutput>& outputs) const;
};
#endif //AVAILABLE_UTXO_COLLECTOR_H