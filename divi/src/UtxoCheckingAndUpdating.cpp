#include <UtxoCheckingAndUpdating.h>

#include <primitives/transaction.h>
#include <Logging.h>
#include <coins.h>
#include <chain.h>
#include <blockmap.h>
#include <IndexDatabaseUpdates.h>
#include <ValidationState.h>
#include <utilmoneystr.h>
#include <undo.h>
#include <chainparams.h>
#include <defaultValues.h>

bool CheckInputs(
    const CTransaction& tx,
    CValidationState& state,
    const CCoinsViewCache& inputs,
    const BlockMap& blockIndexMap,
    bool fScriptChecks,
    unsigned int flags,
    std::vector<CScriptCheck>* pvChecks)
{
    CAmount nFees = 0;
    CAmount nValueIn = 0;
    return CheckInputs(tx, state, inputs, blockIndexMap, nFees, nValueIn, fScriptChecks, flags, pvChecks);
}


bool CheckInputs(
    const CTransaction& tx,
    CValidationState& state,
    const CCoinsViewCache& inputs,
    const BlockMap& blockIndexMap,
    CAmount& nFees,
    CAmount& nValueIn,
    bool fScriptChecks,
    unsigned int flags,
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
                error("CheckInputs() : %s inputs unavailable", tx.ToStringShort()) );
        }


        // While checking, GetBestBlock() refers to the parent block.
        // This is also true for mempool checks.
        const CAmount maxMoneyAllowedInOutput = Params().MaxMoneyOut();
        CBlockIndex* pindexPrev = blockIndexMap.find(inputs.GetBestBlock())->second;
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
            if (!MoneyRange(coins->vout[prevout.n].nValue,maxMoneyAllowedInOutput) || !MoneyRange(nValueIn,maxMoneyAllowedInOutput))
                return state.DoS(100, error("CheckInputs() : txin values out of range"),
                                 REJECT_INVALID, "bad-txns-inputvalues-outofrange");
        }

        if (!tx.IsCoinStake()) {
            if (nValueIn < tx.GetValueOut())
                return state.DoS(100, error("CheckInputs() : %s value in (%s) < value out (%s)",
                                            tx.ToStringShort(), FormatMoney(nValueIn), FormatMoney(tx.GetValueOut())),
                                 REJECT_INVALID, "bad-txns-in-belowout");

            // Tally transaction fees
            CAmount nTxFee = nValueIn - tx.GetValueOut();
            if (nTxFee < 0)
                return state.DoS(100, error("CheckInputs() : %s nTxFee < 0", tx.ToStringShort()),
                                 REJECT_INVALID, "bad-txns-fee-negative");
            nFees += nTxFee;
            if (!MoneyRange(nFees,maxMoneyAllowedInOutput))
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
                CScriptCheck check(*coins, tx, i, flags);
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
                                           flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS);
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
