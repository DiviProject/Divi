#ifndef CACHED_TRANSACTION_DELTAS_CALCULATOR_H
#define CACHED_TRANSACTION_DELTAS_CALCULATOR_H
#include <I_TransactionDetailCalculator.h>

#include <amount.h>
#include <uint256.h>
#include <map>
#include <CachedTransactionDeltas.h>
#include <memory>

class CWalletTx;
class I_UtxoOwnershipDetector;
class I_AppendOnlyTransactionRecord;

class CachedTransactionDeltasCalculator final: public I_CachedTransactionDetailCalculator<CachedTransactionDeltas>
{
private:
    mutable std::map<uint256,std::map<uint8_t, CachedTransactionDeltas>> cachedTransactionDeltasByHash_;
    std::unique_ptr<I_UtxoOwnershipDetector> decoratedOwnershipDetector_;
    const I_AppendOnlyTransactionRecord& txRecord_;
    const CAmount maxMoneyAllowedInOutput_;

    CachedTransactionDeltas recomputeTransactionDeltas(const CWalletTx& wtx, const UtxoOwnershipFilter& requestedFilter) const;
public:
    CachedTransactionDeltasCalculator(
        const I_UtxoOwnershipDetector& ownershipDetector,
        const I_AppendOnlyTransactionRecord& txRecord,
        const CAmount maxMoneyAllowedInOutput);
    ~CachedTransactionDeltasCalculator();
    void calculate(const CWalletTx& transaction, const UtxoOwnershipFilter& ownershipFilter, CachedTransactionDeltas& intermediateResult) const override;
    void recomputeCachedTxEntries(const CWalletTx& transaction) const override;
};
#endif// CACHED_TRANSACTION_DELTAS_CALCULATOR_H