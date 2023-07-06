#include <CachedTransactionDeltasCalculator.h>

#include <I_UtxoOwnershipDetector.h>
#include <I_AppendOnlyTransactionRecord.h>
#include <IsMineType.h>
#include <WalletTx.h>
#include <utilmoneystr.h>

CAmount ComputeDebit(const I_AppendOnlyTransactionRecord& txRecord, const I_UtxoOwnershipDetector& ownershipDetector, const CTxIn& txin, const UtxoOwnershipFilter& filter)
{
    {
        const CWalletTx* txPtr = txRecord.GetWalletTx(txin.prevout.hash);
        if (txPtr != nullptr)
        {
            const CWalletTx& prev = *txPtr;
            if (txin.prevout.n < prev.vout.size())
                if (filter.hasRequested(ownershipDetector.isMine(prev.vout[txin.prevout.n])))
                    return prev.vout[txin.prevout.n].nValue;
        }
    }
    return 0;
}

CAmount ComputeDebit(const I_AppendOnlyTransactionRecord& txRecord, const I_UtxoOwnershipDetector& ownershipDetector, const CTransaction& tx, const UtxoOwnershipFilter& filter, const CAmount maxMoneyAllowedInOutput)
{
    CAmount nDebit = 0;
    for(const CTxIn& txin: tx.vin)
    {
        nDebit += ComputeDebit(txRecord, ownershipDetector, txin, filter);
        if (!MoneyRange(nDebit,maxMoneyAllowedInOutput))
            throw std::runtime_error("CWallet::ComputeDebit() : value out of range");
    }
    return nDebit;
}

CAmount ComputeChange(const I_UtxoOwnershipDetector& ownershipDetector, const CTxOut& txout, const CAmount maxMoneyAllowedInOutput)
{
    if (!MoneyRange(txout.nValue,maxMoneyAllowedInOutput))
        throw std::runtime_error("CWallet::ComputeChange() : value out of range");
    return (ownershipDetector.isChange(txout) ? txout.nValue : 0);
}

CAmount ComputeChange(const I_UtxoOwnershipDetector& ownershipDetector, const CTransaction& tx, const CAmount maxMoneyAllowedInOutput)
{
    CAmount nChange = 0;
    for(const CTxOut& txout: tx.vout)
    {
        nChange += ComputeChange(ownershipDetector, txout, maxMoneyAllowedInOutput);
        if (!MoneyRange(nChange,maxMoneyAllowedInOutput))
            throw std::runtime_error("CWallet::ComputeChange() : value out of range");
    }
    return nChange;
}

CAmount ComputeCredit(const I_UtxoOwnershipDetector& ownershipDetector, const CTxOut& txout, const UtxoOwnershipFilter& filter, const CAmount maxMoneyAllowedInOutput)
{
    if (!MoneyRange(txout.nValue,maxMoneyAllowedInOutput))
        throw std::runtime_error("CWallet::ComputeCredit() : value out of range");
    return ( filter.hasRequested(ownershipDetector.isMine(txout)) ? txout.nValue : 0);
}
CAmount ComputeCredit(const I_UtxoOwnershipDetector& ownershipDetector, const CWalletTx& tx, const UtxoOwnershipFilter& filter, const CAmount maxMoneyAllowedInOutput)
{
    CAmount nCredit = 0;
    for (const CTxOut& out: tx.vout)
    {
        nCredit += ComputeCredit(ownershipDetector, out, filter,maxMoneyAllowedInOutput);
        if (!MoneyRange(nCredit,maxMoneyAllowedInOutput))
            throw std::runtime_error("CWallet::ComputeCredit() : value out of range");
    }
    return nCredit;
}

class VaultAliasingUtxoOwnershipDetector final: public I_UtxoOwnershipDetector
{
private:
    const I_UtxoOwnershipDetector& wrappedOwnershipDetector_;
public:
    VaultAliasingUtxoOwnershipDetector(const I_UtxoOwnershipDetector& wrappedOwnershipDetector): wrappedOwnershipDetector_(wrappedOwnershipDetector) {}
    isminetype isMine(const CTxOut& output) const override
    {
        isminetype mine = wrappedOwnershipDetector_.isMine(output);
        return (mine == isminetype::ISMINE_MANAGED_VAULT || mine == isminetype::ISMINE_OWNED_VAULT)? isminetype::ISMINE_SPENDABLE : mine;
    }
    bool isChange(const CTxOut& output) const override
    {
        return wrappedOwnershipDetector_.isChange(output);
    }
};

CachedTransactionDeltasCalculator::CachedTransactionDeltasCalculator(
    const I_UtxoOwnershipDetector& ownershipDetector,
    const I_AppendOnlyTransactionRecord& txRecord,
    const CAmount maxMoneyAllowedInOutput
    ): cachedTransactionDeltasByHash_()
    , decoratedOwnershipDetector_(new VaultAliasingUtxoOwnershipDetector(ownershipDetector))
    , txRecord_(txRecord)
    , maxMoneyAllowedInOutput_(maxMoneyAllowedInOutput)
{
}

CachedTransactionDeltasCalculator::~CachedTransactionDeltasCalculator()
{
    decoratedOwnershipDetector_.reset();
}

CachedTransactionDeltas CachedTransactionDeltasCalculator::recomputeTransactionDeltas(const CWalletTx& wtx, const UtxoOwnershipFilter& requestedFilter) const
{
    auto txid = wtx.GetHash();
    CachedTransactionDeltas totalDeltas;
    for(auto ownershipType: {isminetype::ISMINE_SPENDABLE, isminetype::ISMINE_WATCH_ONLY})
    {
        UtxoOwnershipFilter filter(ownershipType);
        if(!requestedFilter.hasRequested(ownershipType)) continue;
        const CAmount credit = ComputeCredit(*decoratedOwnershipDetector_, wtx,filter, maxMoneyAllowedInOutput_);
        const CAmount debit = ComputeDebit(txRecord_, *decoratedOwnershipDetector_, wtx, filter, maxMoneyAllowedInOutput_);
        const CAmount changeAmount = ownershipType==isminetype::ISMINE_SPENDABLE? ComputeChange(*decoratedOwnershipDetector_, wtx, maxMoneyAllowedInOutput_):0;
        const CAmount feesPaid = (debit > 0)? debit - changeAmount : 0;

        CachedTransactionDeltas currentDeltas(credit, debit, changeAmount,feesPaid);
        totalDeltas += currentDeltas;
    }
    cachedTransactionDeltasByHash_[txid][requestedFilter.underlyingBitMask()] = totalDeltas;
    return totalDeltas;
}


void CachedTransactionDeltasCalculator::calculate(const CWalletTx& transaction, const UtxoOwnershipFilter& ownershipFilter, CachedTransactionDeltas& intermediateResult) const
{
    auto txid = transaction.GetHash();
    if(cachedTransactionDeltasByHash_.count(txid) > 0 && cachedTransactionDeltasByHash_[txid].count(ownershipFilter.underlyingBitMask()))
    {
        intermediateResult += cachedTransactionDeltasByHash_[txid][ownershipFilter.underlyingBitMask()];
    }
    else
    {
        CachedTransactionDeltas deltas = recomputeTransactionDeltas(transaction,ownershipFilter);
        intermediateResult += deltas;
    }
}
void CachedTransactionDeltasCalculator::recomputeCachedTxEntries(const CWalletTx& transaction) const
{
    cachedTransactionDeltasByHash_.erase(transaction.GetHash());
}