#ifndef AVAILABLE_UTXO_CALCULATOR_H
#define AVAILABLE_UTXO_CALCULATOR_H
#include <I_TransactionDetailCalculator.h>
#include <Output.h>
#include <vector>
#include <LockedCoinsSet.h>
#include <AvailableCoinsType.h>

class I_AppendOnlyTransactionRecord;
class I_MerkleTxConfirmationNumberCalculator;
class I_UtxoOwnershipDetector;
class I_SpentOutputTracker;
class BlockMap;
class CChain;
class CTxIn;
class CTxOut;
class CWalletTx;
class UtxoOwnershipFilter;
enum class isminetype;

class AvailableUtxoCalculator final: public I_TransactionDetailCalculator<std::vector<COutput>>
{
private:
    const I_AppendOnlyTransactionRecord& txRecord_;
    const I_UtxoOwnershipDetector& ownershipDetector_;
    const CAmount minimumVaultAmount_;
    const BlockMap& blockIndexByHash_;
    const CChain& activeChain_;
    const I_SpentOutputTracker& spentOutputTracker_;
    const LockedCoinsSet& lockedCoins_;
    AvailableCoinsType coinType_;
    bool onlyConfirmedTxs_;
    bool requireInputsSpentByMe_;

    bool IsAvailableType(const CTxOut& output, AvailableCoinsType coinType, isminetype& mine) const;
    bool allInputsAreSpendableByMe(const CWalletTx& walletTransaction, const UtxoOwnershipFilter& ownershipFilter) const;

public:
    AvailableUtxoCalculator(
        const BlockMap& blockIndexByHash,
        const CChain& activeChain,
        const CAmount minimumVaultAmount,
        const I_AppendOnlyTransactionRecord& txRecord,
        const I_MerkleTxConfirmationNumberCalculator& confsCalculator,
        const I_UtxoOwnershipDetector& ownershipDetector,
        const I_SpentOutputTracker& spentOutputTracker,
        const LockedCoinsSet& lockedCoins);

    ~AvailableUtxoCalculator() = default;

    void calculate(
        const CWalletTx& walletTransaction,
        const UtxoOwnershipFilter& ownershipFilter,
        std::vector<COutput>& outputs) const override;

    void setRequirements(const AvailableCoinsType coinType, const bool onlyConfirmedTxs, const bool requireInputsSpentByMe);
};
#endif// AVAILABLE_UTXO_CALCULATOR_H