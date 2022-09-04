#include <MempoolConsensus.h>

#include <sync.h>
#include <primitives/transaction.h>
#include <defaultValues.h>
#include <ChainstateManager.h>
#include <script/standard.h>
#include <TransactionFinalityHelpers.h>
#include <FeeAndPriorityCalculator.h>
#include <script/script.h>
#include <txmempool.h>
#include <Settings.h>
#include <chain.h>
#include <utiltime.h>
#include <MemPoolEntry.h>
#include <coins.h>
#include <TransactionOpCounting.h>
#include <UtxoCheckingAndUpdating.h>
#include <script/SignatureCheckers.h>
#include <ValidationState.h>
#include <Logging.h>
#include <BlockCheckingHelpers.h>
#include <NotificationInterface.h>
#include <MainNotificationRegistration.h>

extern CCriticalSection cs_main;
extern Settings& settings;

bool MempoolConsensus::IsStandardTx(const CTransaction& tx, std::string& reason)
{
    static const bool fIsBareMultisigStd = settings.GetBoolArg("-permitbaremultisig", true);
    static const unsigned nMaxDatacarrierBytes =
        settings.GetBoolArg("-datacarrier", true)? settings.GetArg("-datacarriersize", MAX_OP_META_RELAY): 0u;

    AssertLockHeld(cs_main);
    if (tx.nVersion > CTransaction::CURRENT_VERSION || tx.nVersion < 1) {
        reason = "version";
        return false;
    }

    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    // Treat non-final transactions as non-standard to prevent a specific type
    // of double-spend attack, as well as DoS attacks. (if the transaction
    // can't be mined, the attacker isn't expending resources broadcasting it)
    // Basically we don't want to propagate transactions that can't be included in
    // the next block.
    //
    // However, IsFinalTx() is confusing... Without arguments, it uses
    // chainActive.Height() to evaluate nLockTime; when a block is accepted, chainActive.Height()
    // is set to the value of nHeight in the block. However, when IsFinalTx()
    // is called within CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a transaction can
    // be part of the *next* block, we need to call IsFinalTx() with one more
    // than chainActive.Height().
    //
    // Timestamps on the other hand don't get any special treatment, because we
    // can't know what timestamp the next block will have, and there aren't
    // timestamp applications where it matters.
    if (!IsFinalTx(cs_main,tx, chain, chain.Height() + 1)) {
        reason = "non-final";
        return false;
    }

    // Extremely large transactions with lots of inputs can cost the network
    // almost as much to process as they cost the sender in fees, because
    // computing signature hashes is O(ninputs*txsize). Limiting transactions
    // to MAX_STANDARD_TX_SIZE mitigates CPU exhaustion attacks.
    unsigned int sz = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);
    unsigned int nMaxSize = MAX_STANDARD_TX_SIZE;
    if (sz >= nMaxSize) {
        reason = "tx-size";
        return false;
    }

    for (const CTxIn& txin : tx.vin) {
        // Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
        // keys. (remember the 520 byte limit on redeemScript size) That works
        // out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
        // bytes of scriptSig, which we round off to 1650 bytes for some minor
        // future-proofing. That's also enough to spend a 20-of-20
        // CHECKMULTISIG scriptPubKey, though such a scriptPubKey is not
        // considered standard)
        if (txin.scriptSig.size() > 1650) {
            reason = "scriptsig-size";
            return false;
        }
        if (!txin.scriptSig.IsPushOnly()) {
            reason = "scriptsig-not-pushonly";
            return false;
        }
    }

    unsigned int nDataOut = 0;
    txnouttype whichType;
    for(const CTxOut& txout: tx.vout) {
        if (!::IsStandard(txout.scriptPubKey, whichType,nMaxDatacarrierBytes)) {
            reason = "scriptpubkey";
            return false;
        }

        if (whichType == TX_NULL_DATA)
            nDataOut++;
        else if ((whichType == TX_MULTISIG) && (!fIsBareMultisigStd)) {
            reason = "bare-multisig";
            return false;
        } else if (FeeAndPriorityCalculator::instance().IsDust(txout)) {
            reason = "dust";
            return false;
        }
    }

    // only one OP_META txout is permitted
    if (nDataOut > 1) {
        reason = "multi-op-meta";
        return false;
    }

    return true;
}

/**
 * Check transaction inputs to mitigate two
 * potential denial-of-service attacks:
 *
 * 1. scriptSigs with extra data stuffed into them,
 *    not consumed by scriptPubKey (or P2SH script)
 * 2. P2SH scripts with a crazy number of expensive
 *    CHECKSIG/CHECKMULTISIG operations
 */
bool MempoolConsensus::AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs)
{
    if (tx.IsCoinBase() )
        return true; // coinbase has no inputs

    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxOut& prev = mapInputs.GetOutputFor(tx.vin[i]);

        std::vector<std::vector<unsigned char> > vSolutions;
        txnouttype whichType;
        // get the scriptPubKey corresponding to this input:
        const CScript& prevScript = prev.scriptPubKey;
        if (!ExtractScriptPubKeyFormat(prevScript, whichType, vSolutions))
            return false;
        int nArgsExpected = ScriptSigArgsExpected(whichType, vSolutions);
        if (nArgsExpected < 0)
            return false;

        // Transactions with extra stuff in their scriptSigs are
        // non-standard. Note that this EvalScript() call will
        // be quick, because if there are any operations
        // beside "push data" in the scriptSig
        // IsStandard() will have already returned false
        // and this method isn't called.
        std::vector<std::vector<unsigned char> > stack;
        if (!EvalScript(stack, tx.vin[i].scriptSig, false, BaseSignatureChecker()))
            return false;

        if(whichType==TX_VAULT)
        {
            if(stack.size()==0u) return false;
            const auto& lastElement = stack.back();
            if(lastElement.size()>1u) return false;
            if(lastElement.size()==1u && lastElement.back() != 0x01) return false;
        }
        else if (whichType == TX_SCRIPTHASH)
        {
            if (stack.empty())
                return false;
            CScript subscript(stack.back().begin(), stack.back().end());
            std::vector<std::vector<unsigned char> > vSolutions2;
            txnouttype whichType2;
            if (ExtractScriptPubKeyFormat(subscript, whichType2, vSolutions2)) {
                int tmpExpected = ScriptSigArgsExpected(whichType2, vSolutions2);
                if (tmpExpected < 0)
                    return false;
                nArgsExpected += tmpExpected;
            } else {
                // Any other Script with less than 15 sigops OK:
                unsigned int sigops = subscript.GetSigOpCount(true);
                // ... extra data left on the stack after execution is OK, too:
                if (sigops > MAX_P2SH_SIGOPS)
                    return false;
                continue;
            }
        }

        if (stack.size() != (unsigned int)nArgsExpected)
            return false;
    }

    return true;
}


bool TxShouldBePrioritized(const uint256& txHash, unsigned int nBytes, CTxMemPool& pool)
{
    double dPriorityDelta = 0;
    CAmount nFeeDelta = 0;
    {
        LOCK(pool.cs);
        pool.ApplyDeltas(txHash, dPriorityDelta, nFeeDelta);
    }
    if (dPriorityDelta > 0 || nFeeDelta > 0 || nBytes < (DEFAULT_BLOCK_PRIORITY_SIZE - 1000))
        return true;

    return false;
}

bool RateLimiterAllowsFreeTransaction(CValidationState& state, const unsigned nSize)
{
    // Continuously rate-limit free (really, very-low-fee) transactions
    // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
    // be annoying or make others' transactions take longer to confirm.
    static CCriticalSection csFreeLimiter;
    static double dFreeCount;
    static int64_t nLastTime;
    int64_t nNow = GetTime();

    LOCK(csFreeLimiter);

    // Use an exponentially decaying ~10-minute window:
    dFreeCount *= pow(1.0 - 1.0 / 600.0, (double)(nNow - nLastTime));
    nLastTime = nNow;
    // -limitfreerelay unit is thousand-bytes-per-minute
    // At default rate it would take over a month to fill 1GB
    if (dFreeCount >= settings.GetArg("-limitfreerelay", 30) * 10 * 1000)
        return state.DoS(0, error("%s : free transaction rejected by rate limiter",__func__),
                            REJECT_INSUFFICIENTFEE, "rate limited free transaction");
    LogPrint("mempool", "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount + nSize);
    dFreeCount += nSize;
    return true;
}

bool CheckFeesPaidAreAcceptable(
    const CTxMemPoolEntry& entry,
    bool fLimitFree,
    CValidationState& state,
    CTxMemPool& pool)
{
    const unsigned int nSize = entry.GetTxSize();
    const CTransaction& tx = entry.GetTx();
    const CAmount nFees = entry.GetFee();

    static const CFeeRate& minimumRelayFeeRate = FeeAndPriorityCalculator::instance().getMinimumRelayFeeRate();
    const uint256 hash = tx.GetHash();
    CAmount minimumRelayFee = minimumRelayFeeRate.GetFee(nSize);
    bool txShouldHavePriority = TxShouldBePrioritized(hash, nSize, pool);
    if (fLimitFree && !txShouldHavePriority && nFees < minimumRelayFee)
        return state.DoS(0, error("%s : not enough fees %s, %d < %d",__func__,
                                    tx.ToStringShort(), nFees, minimumRelayFee),
                            REJECT_INSUFFICIENTFEE, "insufficient fee");
    // Require that free transactions have sufficient priority to be mined in the next block.
    if (settings.GetBoolArg("-relaypriority", true) &&
        nFees < minimumRelayFee &&
        !CTxMemPoolEntry::AllowFree(entry.ComputeInputCoinAgePerByte(entry.GetHeight() + 1) ))
    {
        return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "insufficient priority");
    }
    if (fLimitFree && nFees < minimumRelayFee && !RateLimiterAllowsFreeTransaction(state,nSize))
    {
        return false;
    }
    return true;
}

bool MempoolConsensus::AcceptToMemoryPool(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree, bool* pfMissingInputs, bool ignoreFees)
{
    AssertLockHeld(cs_main);
    if (pfMissingInputs)
        *pfMissingInputs = false;

    const bool requireStandard = !settings.GetBoolArg("-acceptnonstandard", false);

    if (!CheckTransaction(tx, state))
        return state.DoS(100, error("%s: : CheckTransaction failed",__func__), REJECT_INVALID, "bad-tx");

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
        return state.DoS(100, error("%s: : coinbase as individual tx",__func__),
                         REJECT_INVALID, "coinbase");

    //Coinstake is also only valid in a block, not as a loose transaction
    if (tx.IsCoinStake())
        return state.DoS(100, error("%s: coinstake as individual tx",__func__),
                         REJECT_INVALID, "coinstake");

    // Rather not work on nonstandard transactions (unless -testnet/-regtest)
    std::string reason;
    if (requireStandard && !MempoolConsensus::IsStandardTx(tx, reason))
        return state.DoS(0,
                         error("%s : nonstandard transaction: %s",__func__, reason),
                         REJECT_NONSTANDARD, reason);
    // is it already in the memory pool?
    uint256 hash = tx.GetHash();
    if (pool.exists(hash))
    {
        LogPrint("mempool","%s - tx %s already in mempool\n", __func__,hash);
        return false;
    }

    // Check for conflicts with in-memory transactions
    {
        LOCK(pool.cs); // protect pool.mapNextTx
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            COutPoint outpoint = tx.vin[i].prevout;
            if (pool.mapNextTx.count(outpoint)) {
                // Disable replacement feature for now
                LogPrint("mempool","%s - Conflicting tx spending same inputs %s\n",__func__, hash);
                return false;
            }
        }
    }


    {
        const ChainstateManager::Reference chainstate;

        CCoinsViewBacked temporaryBacking;
        CCoinsViewCache view(&temporaryBacking);
        CAmount nValueIn = 0;
        {
            LOCK(pool.cs);
            const CCoinsViewMemPool viewMemPool(&chainstate->CoinsTip(), pool);
            temporaryBacking.SetBackend(viewMemPool);

            // do we already have it?
            if (view.HaveCoins(hash))
            {
                LogPrint("mempool","%s - tx %s outputs already exist\n",__func__,hash);

                return false;
            }

            // do all inputs exist?
            // Note that this does not check for the presence of actual outputs (see the next check for that),
            // only helps filling in pfMissingInputs (to determine missing vs spent).
            for (const CTxIn txin : tx.vin) {
                if (!view.HaveCoins(txin.prevout.hash)) {
                    if (pfMissingInputs)
                        *pfMissingInputs = true;
                    LogPrint("mempool","%s - unknown tx %s input\n",__func__, txin.prevout.hash);
                    return false;
                }
            }

            // are the actual inputs available?
            if (!view.HaveInputs(tx))
                return state.Invalid(error("%s : inputs already spent",__func__),
                                     REJECT_DUPLICATE, "bad-txns-inputs-spent");

            // Bring the best block into scope
            view.GetBestBlock();

            nValueIn = view.GetValueIn(tx);

            // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
            temporaryBacking.DettachBackend();
        }

        // Check for non-standard pay-to-script-hash in inputs
        if (requireStandard && !MempoolConsensus::AreInputsStandard(tx, view))
            return error("%s: : nonstandard transaction input",__func__);

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
        {
            unsigned int nSigOps = GetLegacySigOpCount(tx);
            unsigned int nMaxSigOps = MAX_TX_SIGOPS_CURRENT;
            nSigOps += GetP2SHSigOpCount(tx, view);
            if(nSigOps > nMaxSigOps)
                return state.DoS(0,
                                 error("%s : too many sigops %s, %d > %d",
                                        __func__,
                                       hash, nSigOps, nMaxSigOps),
                                 REJECT_NONSTANDARD, "bad-txns-too-many-sigops");
        }

        const CAmount nFees = nValueIn - tx.GetValueOut();
        const int64_t height = chainstate->ActiveChain().Height();
        const double coinAge = view.ComputeInputCoinAge(tx, height);
        CTxMemPoolEntry entry(tx, nFees, GetTime(), coinAge, height);

        // Don't accept it if it can't get into a block
        // but prioritise dstx and don't check fees for it
        if (!ignoreFees && !CheckFeesPaidAreAcceptable(entry,fLimitFree,state,pool))
        {
            LogPrint("mempool","%s - Conflicting tx spending same inputs%s",__func__, hash);
            return false;
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        if (!CheckInputs(tx, state, view, chainstate->GetBlockMap(), true, STANDARD_SCRIPT_VERIFY_FLAGS)) {
            return error("%s: : ConnectInputs failed %s",__func__, hash);
        }

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack.
        if (!CheckInputs(tx, state, view, chainstate->GetBlockMap(), true, MANDATORY_SCRIPT_VERIFY_FLAGS)) {
            return error("%s: : BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s",__func__, hash);
        }

        // Store transaction in memory
        pool.addUnchecked(hash, entry);
    }

    GetMainNotificationInterface().SyncTransactions(std::vector<CTransaction>({tx}), NULL,TransactionSyncType::MEMPOOL_TX_ADD);

    return true;
}