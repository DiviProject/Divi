#include <UtxoCheckingAndUpdating.h>

#include <primitives/transaction.h>
#include <Logging.h>
#include <coins.h>
#include <chain.h>
#include <blockmap.h>
#include <ValidationState.h>
#include <utilmoneystr.h>
#include <undo.h>
#include <chainparams.h>
#include <defaultValues.h>

extern BlockMap mapBlockIndex;

void UpdateCoinsWithTransaction(const CTransaction& tx, CCoinsViewCache& inputs, CTxUndo& txundo, int nHeight)
{
    // mark inputs spent
    if (!tx.IsCoinBase() ) {
        txundo.vprevout.reserve(tx.vin.size());
        BOOST_FOREACH (const CTxIn& txin, tx.vin) {
            txundo.vprevout.push_back(CTxInUndo());
            bool ret = inputs.ModifyCoins(txin.prevout.hash)->Spend(txin.prevout.n, txundo.vprevout.back());
            assert(ret);
        }
    }

    // add outputs
    inputs.ModifyCoins(tx.GetHash())->FromTx(tx, nHeight);
}

static bool RemoveTxOutputsFromCache(
    const CTransaction& tx,
    const int blockHeight,
    CCoinsViewCache& view)
{
    bool outputsAvailable = true;
    // Check that all outputs are available and match the outputs in the block itself
    // exactly. Note that transactions with only provably unspendable outputs won't
    // have outputs available even in the block itself, so we handle that case
    // specially with outsEmpty.
    CCoins outsEmpty;
    CCoinsModifier outs = view.ModifyCoins(tx.GetHash());
    outs->ClearUnspendable();

    CCoins outsBlock(tx, blockHeight);
    // The CCoins serialization does not serialize negative numbers.
    // No network rules currently depend on the version here, so an inconsistency is harmless
    // but it must be corrected before txout nversion ever influences a network rule.
    if (outsBlock.nVersion < 0)
        outs->nVersion = outsBlock.nVersion;
    if (*outs != outsBlock)
        outputsAvailable = error("DisconnectBlock() : added transaction mismatch? database corrupted");

    // remove outputs
    outs->Clear();
    return outputsAvailable;
}

static void UpdateCoinsForRestoredInputs(
    const COutPoint& out,
    const CTxInUndo& undo,
    CCoinsModifier& coins,
    bool& fClean)
{
    if (undo.nHeight != 0)
    {
        // undo data contains height: this is the last output of the prevout tx being spent
        if (!coins->IsPruned())
            fClean = fClean && error("DisconnectBlock() : undo data overwriting existing transaction");
        coins->Clear();
        coins->fCoinBase = undo.fCoinBase;
        coins->nHeight = undo.nHeight;
        coins->nVersion = undo.nVersion;
    }
    else
    {
        if (coins->IsPruned())
            fClean = fClean && error("DisconnectBlock() : undo data adding output to missing transaction");
    }

    if (coins->IsAvailable(out.n))
        fClean = fClean && error("DisconnectBlock() : undo data overwriting existing output");

    if (coins->vout.size() < out.n + 1)
        coins->vout.resize(out.n + 1);

    coins->vout[out.n] = undo.txout;
}

TxReversalStatus UpdateCoinsReversingTransaction(const CTransaction& tx, CCoinsViewCache& inputs, const CTxUndo& txundo, int nHeight)
{
    bool fClean = true;
    fClean = fClean && RemoveTxOutputsFromCache(tx,nHeight,inputs);
    if(tx.IsCoinBase()) return fClean? TxReversalStatus::OK : TxReversalStatus::CONTINUE_WITH_ERRORS;
    if (txundo.vprevout.size() != tx.vin.size())
    {
        error("%s : transaction and undo data inconsistent - txundo.vprevout.siz=%d tx.vin.siz=%d",__func__, txundo.vprevout.size(), tx.vin.size());
        return fClean?TxReversalStatus::ABORT_NO_OTHER_ERRORS:TxReversalStatus::ABORT_WITH_OTHER_ERRORS;
    }

    for (unsigned int txInputIndex = tx.vin.size(); txInputIndex-- > 0;)
    {
        const COutPoint& out = tx.vin[txInputIndex].prevout;
        const CTxInUndo& undo = txundo.vprevout[txInputIndex];
        CCoinsModifier coins = inputs.ModifyCoins(out.hash);
        UpdateCoinsForRestoredInputs(out,undo,coins,fClean);
    }
    return fClean? TxReversalStatus::OK : TxReversalStatus::CONTINUE_WITH_ERRORS;
}

bool CheckInputs(
    const CTransaction& tx,
    CValidationState& state,
    const CCoinsViewCache& inputs,
    bool fScriptChecks,
    unsigned int flags,
    bool cacheStore,
    std::vector<CScriptCheck>* pvChecks)
{
    CAmount nFees = 0;
    CAmount nValueIn = 0;
    return CheckInputs(tx,state,inputs,nFees,nValueIn,fScriptChecks,flags,cacheStore,pvChecks);
}


bool CheckInputs(
    const CTransaction& tx,
    CValidationState& state,
    const CCoinsViewCache& inputs,
    CAmount& nFees,
    CAmount& nValueIn,
    bool fScriptChecks,
    unsigned int flags,
    bool cacheStore,
    std::vector<CScriptCheck>* pvChecks,
    bool connectBlockDoS)
{
    if (!tx.IsCoinBase() )
    {
        if (pvChecks)
            pvChecks->reserve(tx.vin.size());

        // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
        // for an attacker to attempt to split the network.
        if (!inputs.HaveInputs(tx))
        {
            if(connectBlockDoS)
            {
                return state.DoS(100, error("%s : inputs missing/spent",__func__),
                                 REJECT_INVALID, "bad-txns-inputs-missingorspent");
            }
            return state.Invalid(
                error("CheckInputs() : %s inputs unavailable", tx.GetHash().ToString()) );
        }


        // While checking, GetBestBlock() refers to the parent block.
        // This is also true for mempool checks.
        CBlockIndex* pindexPrev = mapBlockIndex.find(inputs.GetBestBlock())->second;
        int nSpendHeight = pindexPrev->nHeight + 1;
        for (unsigned int i = 0; i < tx.vin.size(); i++)
        {
            const COutPoint& prevout = tx.vin[i].prevout;
            const CCoins* coins = inputs.AccessCoins(prevout.hash);
            assert(coins);

            // If prev is coinbase, check that it's matured
            if (coins->IsCoinBase() || coins->IsCoinStake())
            {
                if (nSpendHeight - coins->nHeight < Params().COINBASE_MATURITY())
                    return state.Invalid(
                                error("CheckInputs() : tried to spend coinbase at depth %d, coinstake=%d", nSpendHeight - coins->nHeight, coins->IsCoinStake()),
                                REJECT_INVALID, "bad-txns-premature-spend-of-coinbase");
            }

            // Check for negative or overflow input values
            nValueIn += coins->vout[prevout.n].nValue;
            if (!MoneyRange(coins->vout[prevout.n].nValue) || !MoneyRange(nValueIn))
                return state.DoS(100, error("CheckInputs() : txin values out of range"),
                                 REJECT_INVALID, "bad-txns-inputvalues-outofrange");
        }

        if (!tx.IsCoinStake()) {
            if (nValueIn < tx.GetValueOut())
                return state.DoS(100, error("CheckInputs() : %s value in (%s) < value out (%s)",
                                            tx.GetHash().ToString(), FormatMoney(nValueIn), FormatMoney(tx.GetValueOut())),
                                 REJECT_INVALID, "bad-txns-in-belowout");

            // Tally transaction fees
            CAmount nTxFee = nValueIn - tx.GetValueOut();
            if (nTxFee < 0)
                return state.DoS(100, error("CheckInputs() : %s nTxFee < 0", tx.GetHash().ToString()),
                                 REJECT_INVALID, "bad-txns-fee-negative");
            nFees += nTxFee;
            if (!MoneyRange(nFees))
                return state.DoS(100, error("CheckInputs() : nFees out of range"),
                                 REJECT_INVALID, "bad-txns-fee-outofrange");
        }
        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.

        // Skip ECDSA signature verification when connecting blocks
        // before the last block chain checkpoint. This is safe because block merkle hashes are
        // still computed and checked, and any change will be caught at the next checkpoint.
        if (fScriptChecks) {
            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                const COutPoint& prevout = tx.vin[i].prevout;
                const CCoins* coins = inputs.AccessCoins(prevout.hash);
                assert(coins);

                // Verify signature
                CScriptCheck check(*coins, tx, i, flags, cacheStore);
                if (pvChecks) {
                    pvChecks->push_back(CScriptCheck());
                    check.swap(pvChecks->back());
                } else if (!check()) {
                    if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
                        // Check whether the failure was caused by a
                        // non-mandatory script verification check, such as
                        // non-standard DER encodings or non-null dummy
                        // arguments; if so, don't trigger DoS protection to
                        // avoid splitting the network between upgraded and
                        // non-upgraded nodes.
                        CScriptCheck check(*coins, tx, i,
                                           flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, cacheStore);
                        if (check())
                            return state.Invalid(false, REJECT_NONSTANDARD, strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(check.GetScriptError())));
                    }
                    // Failures of other flags indicate a transaction that is
                    // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
                    // such nodes as they are not following the protocol. That
                    // said during an upgrade careful thought should be taken
                    // as to the correct behavior - we may want to continue
                    // peering with non-upgraded nodes even after a soft-fork
                    // super-majority vote has passed.
                    return state.DoS(100, false, REJECT_INVALID, strprintf("mandatory-script-verify-flag-failed (%s)", ScriptErrorString(check.GetScriptError())));
                }
            }
        }
    }

    return true;
}
