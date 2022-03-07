// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"

#include <ActiveChainManager.h>
#include "addrman.h"
#include "alert.h"
#include <blockmap.h>
#include "BlockFileOpener.h"
#include "BlockDiskAccessor.h"
#include <BlockRejects.h>
#include "BlockRewards.h"
#include "BlockSigning.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "checkqueue.h"
#include "coins.h"
#include <defaultValues.h>
#include "FeeRate.h"
#include "init.h"
#include "kernel.h"
#include "masternode-payments.h"
#include "masternodeman.h"
#include "merkleblock.h"
#include "net.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/sigcache.h"
#include "script/standard.h"
#include "spork.h"
#include "sporkdb.h"
#include "sync.h"
#include "tinyformat.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "undo.h"
#include "util.h"
#include "utilmoneystr.h"
#include <UtxoCheckingAndUpdating.h>
#include "NotificationInterface.h"
#include "FeeAndPriorityCalculator.h"
#include <SuperblockSubsidyContainer.h>
#include <BlockIncentivesPopulator.h>
#include <BlockIndexLotteryUpdater.h>
#include <sstream>
#include "Settings.h"
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <BlockUndo.h>
#include <ValidationState.h>
#include <scriptCheck.h>
#include <BlockFileInfo.h>
#include <TransactionOpCounting.h>
#include <OrphanTransactions.h>
#include <MasternodeModule.h>
#include <IndexDatabaseUpdates.h>
#include <BlockTransactionChecker.h>
#include <NodeState.h>
#include <PeerBanningService.h>
#include <utilstrencodings.h>
#include <NodeStateRegistry.h>
#include <Node.h>
#include <TransactionSearchIndexes.h>
#include <ProofOfStakeModule.h>
#include <BlockFileHelpers.h>

using namespace boost;
using namespace std;

extern Settings& settings;

#if defined(NDEBUG)
#error "DIVI cannot be compiled without assertions."
#endif

// 6 comes from OPCODE (1) + vch.size() (1) + BIGNUM size (4)
#define SCRIPT_OFFSET 6
// For Script size (BIGNUM/Uint256 size)
#define BIGNUM_SIZE   4
/**
 * Global state
 */

CCriticalSection cs_main;
std::map<uint256, uint256> mapProofOfStake;
int64_t timeOfLastChainTipUpdate =0;
const CBlockIndex* pindexBestHeader = nullptr;
CWaitableCriticalSection csBestBlock;
CConditionVariable cvBlockChange;
int nScriptCheckThreads = 0;
bool fImporting = false;
bool fReindex = false;
bool fTxIndex = true;
bool fCheckBlockIndex = false;
bool fVerifyingBlocks = false;
unsigned int nCoinCacheSize = 5000;

extern bool fAddressIndex;
extern bool fSpentIndex;
extern CTxMemPool mempool;

bool IsFinalTx(const CTransaction& tx, const CChain& activeChain, int nBlockHeight = 0 , int64_t nBlockTime = 0);

CCheckpointServices checkpointsVerifier(GetCurrentChainCheckpoints);

static void CheckBlockIndex();

std::map<uint256, int64_t> mapRejectedBlocks;
// Internal stuff
namespace
{
struct CBlockIndexWorkComparator {
    bool operator()(const CBlockIndex* pa, const CBlockIndex* pb) const
    {
        // First sort by most total work, ...
        if (pa->nChainWork > pb->nChainWork) return false;
        if (pa->nChainWork < pb->nChainWork) return true;

        // ... then by earliest time received, ...
        if (pa->nSequenceId < pb->nSequenceId) return false;
        if (pa->nSequenceId > pb->nSequenceId) return true;

        // Use pointer address as tie breaker (should only happen with blocks
        // loaded from disk, as those all have id 0).
        if (pa < pb) return false;
        if (pa > pb) return true;

        // Identical blocks.
        return false;
    }
};

const CBlockIndex* pindexBestInvalid;

/**
     * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
     * as good as our current tip or better. Entries may be failed, though.
     */
std::set<CBlockIndex*, CBlockIndexWorkComparator> setBlockIndexCandidates;
/** All pairs A->B, where A (or one if its ancestors) misses transactions, but B has transactions. */
std::multimap<CBlockIndex*, CBlockIndex*> mapBlocksUnlinked;

/**
     * Every received block is assigned a unique and increasing identifier, so we
     * know which one to give priority in case of a fork.
     */
CCriticalSection cs_nBlockSequenceId;
/** Blocks loaded from disk are assigned id 0, so start the counter at 1. */
uint32_t nBlockSequenceId = 1;

/**
     * Sources of received blocks, to be able to send them reject messages or ban
     * them, if processing happens afterwards. Protected by cs_main.
     */
std::map<uint256, NodeId> mapBlockSource;

} // anon namespace

static bool UpdateDBIndicesForNewBlock(
    const IndexDatabaseUpdates& indexDatabaseUpdates,
    CBlockTreeDB& blockTreeDatabase,
    CValidationState& state)
{
    if (fTxIndex)
        if (!blockTreeDatabase.WriteTxIndex(indexDatabaseUpdates.txLocationData))
            return state.Abort("ConnectingBlock: Failed to write transaction index");

    if (fAddressIndex) {
        if (!blockTreeDatabase.WriteAddressIndex(indexDatabaseUpdates.addressIndex)) {
            return state.Abort("ConnectingBlock: Failed to write address index");
        }

        if (!blockTreeDatabase.UpdateAddressUnspentIndex(indexDatabaseUpdates.addressUnspentIndex)) {
            return state.Abort("ConnectingBlock: Failed to write address unspent index");
        }
    }

    if (fSpentIndex)
        if (!blockTreeDatabase.UpdateSpentIndex(indexDatabaseUpdates.spentIndex))
            return state.Abort("ConnectingBlock: Failed to write update spent index");

    return true;
}
//////////////////////////////////////////////////////////////////////////////
//
// dispatching functions
//

// These functions dispatch to one or all registered wallets

NotificationInterfaceRegistry registry;
MainNotificationSignals& g_signals = registry.getSignals();

void RegisterValidationInterface(NotificationInterface* pwalletIn)
{
    registry.RegisterValidationInterface(pwalletIn);
}

void UnregisterValidationInterface(NotificationInterface* pwalletIn)
{
    registry.UnregisterValidationInterface(pwalletIn);
}

void UnregisterAllValidationInterfaces()
{
    registry.UnregisterAllValidationInterfaces();
}

//////////////////////////////////////////////////////////////////////////////
//
// Registration of network node signals.
//
int GetHeight()
{
    const ChainstateManager::Reference chainstate;
    while (true) {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) {
            MilliSleep(50);
            continue;
        }
        return chainstate->ActiveChain().Height();
    }
}

const CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator)
{
    const ChainstateManager::Reference chainstate;
    const auto& blockMap = chainstate->GetBlockMap();

    // Find the first block the caller has in the main chain
    for(const uint256& hash: locator.vHave) {
        const auto mi = blockMap.find(hash);
        if (mi != blockMap.end()) {
            const CBlockIndex* pindex = mi->second;
            if (chain.Contains(pindex))
                return pindex;
        }
    }
    return chain.Genesis();
}

bool IsStandardTx(const CTransaction& tx, string& reason)
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
    if (!IsFinalTx(tx, chain, chain.Height() + 1)) {
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
bool AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs)
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

bool CheckTransaction(const CTransaction& tx, CValidationState& state,std::set<COutPoint>& usedInputsSet)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.DoS(10, error("%s : vin empty",__func__),
                         REJECT_INVALID, "bad-txns-vin-empty");
    if (tx.vout.empty())
        return state.DoS(10, error("%s : vout empty",__func__),
                         REJECT_INVALID, "bad-txns-vout-empty");

    // Size limits
    unsigned int nMaxSize = MAX_STANDARD_TX_SIZE;

    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > nMaxSize)
        return state.DoS(100, error("%s : size limits failed",__func__),
                         REJECT_INVALID, "bad-txns-oversize");

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    const CAmount maxMoneyAllowedInOutput = Params().MaxMoneyOut();
    for(const CTxOut& txout: tx.vout)
    {
        if (txout.IsEmpty() && !tx.IsCoinBase() && !tx.IsCoinStake())
            return state.DoS(100, error("%s: txout empty for user transaction",__func__));
        if(!MoneyRange(txout.nValue,maxMoneyAllowedInOutput))
            return state.DoS(100, error("%s : txout.nValue out of range",__func__),
                             REJECT_INVALID, "bad-txns-vout-negative-or-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut,maxMoneyAllowedInOutput))
            return state.DoS(100, error("%s : txout total out of range",__func__),
                             REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    // Check for duplicate inputs
    for (const CTxIn& txin : tx.vin) {
        if (usedInputsSet.count(txin.prevout))
            return state.DoS(100, error("%s : duplicate inputs",__func__),
                             REJECT_INVALID, "bad-txns-inputs-duplicate");

        usedInputsSet.insert(txin.prevout);
    }

    if (tx.IsCoinBase()) {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 150)
            return state.DoS(100, error("%s : coinbase script size=%d",__func__, tx.vin[0].scriptSig.size()),
                    REJECT_INVALID, "bad-cb-length");
    }

    return true;
}
bool CheckTransaction(const CTransaction& tx, CValidationState& state)
{
    std::set<COutPoint> vInOutPoints;
    return CheckTransaction(tx,state,vInOutPoints);
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

bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree, bool* pfMissingInputs, bool ignoreFees)
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
    string reason;
    if (requireStandard && !IsStandardTx(tx, reason))
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
        if (requireStandard && !AreInputsStandard(tx, view))
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
        pool.addUnchecked(hash, entry, view);
    }

    g_signals.SyncTransactions(std::vector<CTransaction>({tx}), NULL,TransactionSyncType::MEMPOOL_TX_ADD);

    return true;
}

//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

bool IsInitialBlockDownload()	//2446
{
    LOCK(cs_main);

    const ChainstateManager::Reference chainstate;
    const int64_t height = chainstate->ActiveChain().Height();

    if (fImporting || fReindex || fVerifyingBlocks || height < checkpointsVerifier.GetTotalBlocksEstimate())
        return true;
    static bool lockIBDState = false;
    if (lockIBDState)
        return false;
    bool state = (height < pindexBestHeader->nHeight - 24 * 6 ||
                  pindexBestHeader->GetBlockTime() < GetTime() - 6 * 60 * 60); // ~144 blocks behind -> 2 x fork detection time
    if (!state)
        lockIBDState = true;
    return state;
}

namespace
{

bool fLargeWorkForkFound = false;
bool fLargeWorkInvalidChainFound = false;
const CBlockIndex* pindexBestForkTip = nullptr;
const CBlockIndex* pindexBestForkBase = nullptr;

} // anonymous namespace

void CheckForkWarningConditions()
{
    AssertLockHeld(cs_main);
    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before the last checkpoint)
    if (IsInitialBlockDownload())
        return;

    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    // If our best fork is no longer within 72 blocks (+/- 3 hours if no one mines it)
    // of our head, drop it
    if (pindexBestForkTip && chain.Height() - pindexBestForkTip->nHeight >= 72)
        pindexBestForkTip = NULL;

    if (pindexBestForkTip || (pindexBestInvalid && pindexBestInvalid->nChainWork > chain.Tip()->nChainWork + (GetBlockProof(*chain.Tip()) * 6))) {
        if (!fLargeWorkForkFound && pindexBestForkBase) {
            if (pindexBestForkBase->phashBlock) {
                std::string warning = std::string("'Warning: Large-work fork detected, forking after block ") +
                        pindexBestForkBase->phashBlock->ToString() + std::string("'");
                CAlert::Notify(settings,warning, true);
            }
        }
        if (pindexBestForkTip && pindexBestForkBase) {
            if (pindexBestForkBase->phashBlock) {
                LogPrintf("CheckForkWarningConditions: Warning: Large valid fork found\n  forking the chain at height %d (%s)\n  lasting to height %d (%s).\nChain state database corruption likely.\n",
                          pindexBestForkBase->nHeight, *pindexBestForkBase->phashBlock,
                          pindexBestForkTip->nHeight, *pindexBestForkTip->phashBlock);
                fLargeWorkForkFound = true;
            }
        } else {
            LogPrintf("CheckForkWarningConditions: Warning: Found invalid chain at least ~6 blocks longer than our best chain.\nChain state database corruption likely.\n");
            fLargeWorkInvalidChainFound = true;
        }
    } else {
        fLargeWorkForkFound = false;
        fLargeWorkInvalidChainFound = false;
    }
}

static void CheckForkWarningConditionsOnNewFork(const CBlockIndex* pindexNewForkTip)
{
    AssertLockHeld(cs_main);

    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    // If we are on a fork that is sufficiently large, set a warning flag
    const CBlockIndex* pfork = pindexNewForkTip;
    const CBlockIndex* plonger = chain.Tip();
    while (pfork && pfork != plonger) {
        while (plonger && plonger->nHeight > pfork->nHeight)
            plonger = plonger->pprev;
        if (pfork == plonger)
            break;
        pfork = pfork->pprev;
    }

    // We define a condition which we should warn the user about as a fork of at least 7 blocks
    // who's tip is within 72 blocks (+/- 3 hours if no one mines it) of ours
    // or a chain that is entirely longer than ours and invalid (note that this should be detected by both)
    // We use 7 blocks rather arbitrarily as it represents just under 10% of sustained network
    // hash rate operating on the fork.
    // We define it this way because it allows us to only store the highest fork tip (+ base) which meets
    // the 7-block condition and from this always have the most-likely-to-cause-warning fork
    if (pfork && (!pindexBestForkTip || (pindexBestForkTip && pindexNewForkTip->nHeight > pindexBestForkTip->nHeight)) &&
            pindexNewForkTip->nChainWork - pfork->nChainWork > (GetBlockProof(*pfork) * 7) &&
            chain.Height() - pindexNewForkTip->nHeight < 72) {
        pindexBestForkTip = pindexNewForkTip;
        pindexBestForkBase = pfork;
    }

    CheckForkWarningConditions();
}

namespace
{

void InvalidChainFound(CBlockIndex* pindexNew)
{
    if (!pindexBestInvalid || pindexNew->nChainWork > pindexBestInvalid->nChainWork)
        pindexBestInvalid = pindexNew;

    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    LogPrintf("InvalidChainFound: invalid block=%s  height=%d  log2_work=%.8g  date=%s\n",
              pindexNew->GetBlockHash(), pindexNew->nHeight,
              log(pindexNew->nChainWork.getdouble()) / log(2.0), DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
                                                                                   pindexNew->GetBlockTime()));
    LogPrintf("InvalidChainFound:  current best=%s  height=%d  log2_work=%.8g  date=%s\n",
              chain.Tip()->GetBlockHash(), chain.Height(), log(chain.Tip()->nChainWork.getdouble()) / log(2.0),
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chain.Tip()->GetBlockTime()));
    CheckForkWarningConditions();
}

bool FindBlockPos(CValidationState& state, CDiskBlockPos& pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown = false)
{
    if(fKnown) return BlockFileHelpers::FindKnownBlockPos(state,pos,nAddSize,nHeight,nTime);
    else return BlockFileHelpers::FindUnknownBlockPos(state,pos,nAddSize,nHeight,nTime);
}

//! List of asynchronously-determined block rejections to notify this peer about.
CCriticalSection cs_RejectedBlocks;
std::map<NodeId, std::vector<CBlockReject>> rejectedBlocksByNodeId;
void InvalidBlockFound(CBlockIndex* pindex, const CValidationState& state)
{
    int nDoS = 0;
    if (state.IsInvalid(nDoS)) {
        std::map<uint256, NodeId>::iterator it = mapBlockSource.find(pindex->GetBlockHash());
        if (it != mapBlockSource.end()) {
            if(Misbehaving(it->second,nDoS,"Invalid block sourced from peer"))
            {
                LOCK(cs_RejectedBlocks);
                std::vector<CBlockReject>& rejectedBlocks = rejectedBlocksByNodeId[it->second];
                rejectedBlocks.emplace_back(
                    state.GetRejectCode(),
                    state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH),
                    pindex->GetBlockHash());
            }
        }
    }
    if (!state.CorruptionPossible()) {
        pindex->nStatus |= BLOCK_FAILED_VALID;
        BlockFileHelpers::RecordDirtyBlockIndex(pindex);
        setBlockIndexCandidates.erase(pindex);
        InvalidChainFound(pindex);
    }
}

int64_t nTimeTotal = 0;

void VerifyBestBlockIsAtPreviousBlock(const CBlockIndex* pindex, CCoinsViewCache& view)
{
    const uint256 hashPrevBlock = pindex->pprev == NULL ? uint256(0) : pindex->pprev->GetBlockHash();
    if (hashPrevBlock != view.GetBestBlock())
        LogPrintf("%s: hashPrev=%s view=%s\n", __func__, hashPrevBlock, view.GetBestBlock());
    assert(hashPrevBlock == view.GetBestBlock());
}

bool CheckEnforcedPoSBlocksAndBIP30(const CChainParams& chainParameters, const CBlock& block, CValidationState& state, const CBlockIndex* pindex, const CCoinsViewCache& view)
{
    if (pindex->nHeight <= chainParameters.LAST_POW_BLOCK() && block.IsProofOfStake())
        return state.DoS(100, error("%s : PoS period not active",__func__),
                         REJECT_INVALID, "PoS-early");

    if (pindex->nHeight > chainParameters.LAST_POW_BLOCK() && block.IsProofOfWork())
        return state.DoS(100, error("%s : PoW period ended",__func__),
                         REJECT_INVALID, "PoW-ended");

    // Enforce BIP30.
    for (const auto& tx : block.vtx) {
        const CCoins* coins = view.AccessCoins(tx.GetHash());
        if (coins && !coins->IsPruned())
            return state.DoS(100, error("%s : tried to overwrite transaction (%s)",__func__, tx.GetHash().ToString()),
                             REJECT_INVALID, "bad-txns-BIP30");
    }

    return true;
}

void CalculateFees(bool isProofOfWork, const CBlockIndex* pindex, CBlockRewards& nExpectedMint)
{
    const CAmount nMoneySupplyPrev = pindex->pprev ? pindex->pprev->nMoneySupply : 0;
    CAmount nFees = pindex->nMint - (pindex->nMoneySupply - nMoneySupplyPrev);
    //PoW phase redistributed fees to miner. PoS stage destroys fees.
    if (isProofOfWork)
        nExpectedMint.nStakeReward += nFees;
}

bool WriteUndoDataToDisk(CBlockIndex* pindex, CValidationState& state, CBlockUndo& blockundo)
{
    if (pindex->GetUndoPos().IsNull() || !pindex->IsValid(BLOCK_VALID_SCRIPTS)) {
        if (pindex->GetUndoPos().IsNull()) {
            CDiskBlockPos pos;
            if (!BlockFileHelpers::AllocateDiskSpaceForBlockUndo(pindex->nFile, pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
            {
                return state.Abort("Disk space is low!");
            }
            if (!blockundo.WriteToDisk(pos, pindex->pprev->GetBlockHash()))
                return state.Abort("Failed to write undo data");

            // update nUndoPos in block index
            pindex->nUndoPos = pos.nPos;
            pindex->nStatus |= BLOCK_HAVE_UNDO;
        }

        pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
        BlockFileHelpers::RecordDirtyBlockIndex(pindex);
    }
    return true;
}

bool CheckMintTotalsAndBlockPayees(
    const CBlock& block,
    const CBlockIndex* pindex,
    const BlockIncentivesPopulator& incentives,
    const CBlockRewards& nExpectedMint,
    CValidationState& state)
{
    const auto& coinbaseTx = (pindex->nHeight > Params().LAST_POW_BLOCK() ? block.vtx[1] : block.vtx[0]);

    if (!incentives.IsBlockValueValid(nExpectedMint, pindex->nMint, pindex->nHeight)) {
        return state.DoS(100,
                         error("%s : reward pays too much (actual=%s vs limit=%s)",
                            __func__,
                            FormatMoney(pindex->nMint), nExpectedMint),
                         REJECT_INVALID, "bad-cb-amount");
    }

    if (!incentives.HasValidPayees(coinbaseTx,pindex)) {
        mapRejectedBlocks.insert(std::make_pair(block.GetHash(), GetTime()));
        return state.DoS(0, error("%s: couldn't find masternode or superblock payments",__func__),
                         REJECT_INVALID, "bad-cb-payee");
    }
    return true;
}

} // anonymous namespace

const ActiveChainManager& GetActiveChainManager()
{
    static const BlockDiskDataReader blockDiskReader;
    static auto& chainstate = ChainstateManager::Get();
    static ActiveChainManager chainManager(fAddressIndex,fSpentIndex, &chainstate.BlockTree(), blockDiskReader);
    return chainManager;
}

bool ConnectBlock(
    const CBlock& block,
    CValidationState& state,
    CBlockIndex* pindex,
    CCoinsViewCache& view,
    bool fJustCheck,
    bool fAlreadyChecked)
{
    AssertLockHeld(cs_main);
    ChainstateManager::Reference chainstate;

    // Check it again in case a previous version let a bad block in
    if (!fAlreadyChecked && !CheckBlock(block, state))
        return false;

    static const CChainParams& chainParameters = Params();
    VerifyBestBlockIsAtPreviousBlock(pindex,view);
    if (block.GetHash() == Params().HashGenesisBlock())
    {
        view.SetBestBlock(pindex->GetBlockHash());
        return true;
    }
    if(!CheckEnforcedPoSBlocksAndBIP30(chainParameters,block,state,pindex,view))
    {
        return false;
    }

    static const SuperblockSubsidyContainer subsidiesContainer(chainParameters);
    static const BlockIncentivesPopulator incentives(
        chainParameters,
        GetMasternodeModule(),
        subsidiesContainer.superblockHeightValidator(),
        subsidiesContainer.blockSubsidiesProvider());

    const int blocksToSkipChecksFor = checkpointsVerifier.GetTotalBlocksEstimate();
    IndexDatabaseUpdates indexDatabaseUpdates;
    CBlockRewards nExpectedMint = subsidiesContainer.blockSubsidiesProvider().GetBlockSubsidity(pindex->nHeight);
    if(ActivationState(pindex->pprev).IsActive(Fork::DeprecateMasternodes))
    {
        nExpectedMint.nStakeReward += nExpectedMint.nMasternodeReward;
        nExpectedMint.nMasternodeReward = 0;
    }
    BlockTransactionChecker blockTxChecker(block, state, pindex, view, chainstate->GetBlockMap(), blocksToSkipChecksFor);

    if(!blockTxChecker.Check(nExpectedMint, indexDatabaseUpdates))
    {
        return false;
    }
    CalculateFees(block.IsProofOfWork(),pindex,nExpectedMint);
    if (!CheckMintTotalsAndBlockPayees(block,pindex,incentives,nExpectedMint,state))
        return false;

    if (!fVerifyingBlocks) {
        if (block.nAccumulatorCheckpoint != pindex->pprev->nAccumulatorCheckpoint)
            return state.DoS(100, error("%s : new accumulator checkpoint generated on a block that is not multiple of 10",__func__));
    }

    if (!blockTxChecker.WaitForScriptsToBeChecked())
        return state.DoS(100, false);

    if (!fJustCheck) {
        if(!WriteUndoDataToDisk(pindex,state,blockTxChecker.getBlockUndoData()) ||
           !UpdateDBIndicesForNewBlock(indexDatabaseUpdates, chainstate->BlockTree(), state))
        {
            return false;
        }
    }

    // add this block to the view's block chain
    view.SetBestBlock(pindex->GetBlockHash());

    return true;
}

/**
 * Update the on-disk chain state.
 * The caches and indexes are flushed if either they're too large, forceWrite is set, or
 * fast is not set and it's been a while since the last write.
 */
bool static FlushStateToDisk(CValidationState& state, FlushStateMode mode)
{
    LOCK(cs_main);

    ChainstateManager::Reference chainstate;
    auto& coinsTip = chainstate->CoinsTip();
    auto& blockTreeDB = chainstate->BlockTree();

    static int64_t nLastWrite = 0;
    try {
        if ((mode == FLUSH_STATE_ALWAYS) ||
            ((mode == FLUSH_STATE_PERIODIC || mode == FLUSH_STATE_IF_NEEDED) && coinsTip.GetCacheSize() > nCoinCacheSize) ||
            (mode == FLUSH_STATE_PERIODIC && GetTimeMicros() > nLastWrite + DATABASE_WRITE_INTERVAL * 1000000))
        {
            // Typical CCoins structures on disk are around 100 bytes in size.
            // Pushing a new one to the database can cause it to be written
            // twice (once in the log, and once in the tables). This is already
            // an overestimation, as most will delete an existing entry or
            // overwrite one. Still, use a conservative safety factor of 2.
            if (!CheckDiskSpace(100 * 2 * 2 * coinsTip.GetCacheSize()))
            {
                return state.Abort("Disk space is low!");
            }
            // First make sure all block and undo data is flushed to disk.
            // Then update all block file information (which may refer to block and undo files).
            if(!BlockFileHelpers::WriteBlockFileToBlockTreeDatabase(state,blockTreeDB))
            {
                return false;
            }
            blockTreeDB.Sync();
            // Finally flush the chainstate (which may refer to block index entries).
            if (!coinsTip.Flush())
                return state.Abort("Failed to write to coin database");
            // Update best block in wallet (so we can detect restored wallets).
            if (mode != FLUSH_STATE_IF_NEEDED) {
                g_signals.SetBestChain(chainstate->ActiveChain().GetLocator());
            }
            nLastWrite = GetTimeMicros();
        }
    } catch (const std::runtime_error& e) {
        return state.Abort(std::string("System error while flushing: ") + e.what());
    }
    return true;
}

void FlushStateToDisk(FlushStateMode mode)
{
    CValidationState state;
    FlushStateToDisk(state, mode);
}

/** Update chainActive and related internal data structures. */
void static UpdateTip(const CBlockIndex* pindexNew)
{
    ChainstateManager::Reference chainstate;
    auto& chain = chainstate->ActiveChain();
    chain.SetTip(pindexNew);

    // New best block
    LogPrintf("UpdateTip: new best=%s  height=%d  log2_work=%.8g  tx=%lu  date=%s progress=%f  cache=%u\n",
              chain.Tip()->GetBlockHash(), chain.Height(), log(chain.Tip()->nChainWork.getdouble()) / log(2.0), (unsigned long)chain.Tip()->nChainTx,
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chain.Tip()->GetBlockTime()),
              checkpointsVerifier.GuessVerificationProgress(chain.Tip()), (unsigned int)chainstate->CoinsTip().GetCacheSize());

    cvBlockChange.notify_all();

    // Check the version of the last 100 blocks to see if we need to upgrade:
    static bool fWarned = false;
    if (!IsInitialBlockDownload() && !fWarned) {
        int nUpgraded = 0;
        const CBlockIndex* pindex = chain.Tip();
        for (int i = 0; i < 100 && pindex != NULL; i++) {
            if (pindex->nVersion > CBlock::CURRENT_VERSION)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            LogPrintf("SetBestChain: %d of last 100 blocks above version %d\n", nUpgraded, (int)CBlock::CURRENT_VERSION);
        if (nUpgraded > 100 / 2) {
            // strMiscWarning is read by GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
            strMiscWarning = translate("Warning: This version is obsolete, upgrade required!");
            CAlert::Notify(settings,strMiscWarning, true);
            fWarned = true;
        }
    }
}

/** Disconnect chainActive's tip. */
bool static DisconnectTip(CValidationState& state)
{
    AssertLockHeld(cs_main);

    ChainstateManager::Reference chainstate;
    auto& coinsTip = chainstate->CoinsTip();
    const auto& blockMap = chainstate->GetBlockMap();
    const auto& chain = chainstate->ActiveChain();

    const CBlockIndex* pindexDelete = chain.Tip();
    assert(pindexDelete);
    mempool.check(&coinsTip, blockMap);
    // Read block from disk.
    const ActiveChainManager& chainManager = GetActiveChainManager();
    std::pair<CBlock,bool> disconnectedBlock;
    {
         CCoinsViewCache view(&coinsTip);
         chainManager.DisconnectBlock(disconnectedBlock,state, pindexDelete, view);
         if(!disconnectedBlock.second)
            return error("DisconnectTip() : DisconnectBlock %s failed", pindexDelete->GetBlockHash());
         assert(view.Flush());
    }
    std::vector<CTransaction>& blockTransactions = disconnectedBlock.first.vtx;

    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_ALWAYS))
        return false;
    // Resurrect mempool transactions from the disconnected block.
    for(const CTransaction& tx: blockTransactions) {
        // ignore validation errors in resurrected transactions
        std::list<CTransaction> removed;
        CValidationState stateDummy;
        if (tx.IsCoinBase() || tx.IsCoinStake() || !AcceptToMemoryPool(mempool, stateDummy, tx, false))
            mempool.remove(tx, removed, true);
    }
    mempool.removeCoinbaseSpends(&coinsTip, pindexDelete->nHeight);
    mempool.check(&coinsTip, blockMap);
    // Update chainActive and related variables.
    UpdateTip(pindexDelete->pprev);
    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    g_signals.SyncTransactions(blockTransactions, NULL,TransactionSyncType::BLOCK_DISCONNECT);
    return true;
}

static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;

/**
 * Connect a new block to chainActive. pblock is either NULL or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk.
 */
bool static ConnectTip(CValidationState& state, CBlockIndex* pindexNew, const CBlock* pblock, bool fAlreadyChecked)
{
    AssertLockHeld(cs_main);

    ChainstateManager::Reference chainstate;
    auto& coinsTip = chainstate->CoinsTip();
    const auto& blockMap = chainstate->GetBlockMap();

    assert(pindexNew->pprev == chainstate->ActiveChain().Tip());
    mempool.check(&coinsTip, blockMap);
    CCoinsViewCache view(&coinsTip);

    if (pblock == NULL)
        fAlreadyChecked = false;

    // Read block from disk.
    int64_t nTime1 = GetTimeMicros();
    CBlock block;
    if (!pblock) {
        if (!ReadBlockFromDisk(block, pindexNew))
            return state.Abort("Failed to read block");
        pblock = &block;
    }
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros();
    nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    LogPrint("bench", "  - Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * 0.001, nTimeReadFromDisk * 0.000001);
    {
        CInv inv(MSG_BLOCK, pindexNew->GetBlockHash());
        bool rv = ConnectBlock(*pblock, state, pindexNew, view, false, fAlreadyChecked);
        if (!rv) {
            if (state.IsInvalid())
                InvalidBlockFound(pindexNew, state);
            return error("%s : ConnectBlock %s failed",__func__, pindexNew->GetBlockHash());
        }
        mapBlockSource.erase(inv.GetHash());
        nTime3 = GetTimeMicros();
        nTimeConnectTotal += nTime3 - nTime2;
        LogPrint("bench", "  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001, nTimeConnectTotal * 0.000001);
        assert(view.Flush());
    }
    int64_t nTime4 = GetTimeMicros();
    nTimeFlush += nTime4 - nTime3;
    LogPrint("bench", "  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);

    // Write the chain state to disk, if necessary. Always write to disk if this is the first of a new file.
    FlushStateMode flushMode = FLUSH_STATE_IF_NEEDED;
    if (pindexNew->pprev && (pindexNew->GetBlockPos().nFile != pindexNew->pprev->GetBlockPos().nFile))
        flushMode = FLUSH_STATE_ALWAYS;
    if (!FlushStateToDisk(state, flushMode))
        return false;
    int64_t nTime5 = GetTimeMicros();
    nTimeChainState += nTime5 - nTime4;
    LogPrint("bench", "  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001, nTimeChainState * 0.000001);

    // Remove conflicting transactions from the mempool.
    std::list<CTransaction> txConflicted;
    mempool.removeConfirmedTransactions(pblock->vtx, pindexNew->nHeight, txConflicted);
    mempool.check(&coinsTip, blockMap);
    // Update chainActive & related variables.
    UpdateTip(pindexNew);
    // Tell wallet about transactions that went from mempool
    // to conflicted:
    std::vector<CTransaction> conflictedTransactions(txConflicted.begin(),txConflicted.end());
    g_signals.SyncTransactions(conflictedTransactions, NULL,TransactionSyncType::CONFLICTED_TX);
    // ... and about transactions that got confirmed:
    g_signals.SyncTransactions(pblock->vtx, pblock, TransactionSyncType::NEW_BLOCK);

    int64_t nTime6 = GetTimeMicros();
    nTimePostConnect += nTime6 - nTime5;
    nTimeTotal += nTime6 - nTime1;
    LogPrint("bench", "  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001, nTimePostConnect * 0.000001);
    LogPrint("bench", "- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);
    return true;
}

bool DisconnectBlocksAndReprocess(int blocks)
{
    LOCK(cs_main);

    CValidationState state;

    LogPrintf("DisconnectBlocksAndReprocess: Got command to replay %d blocks\n", blocks);
    for (int i = 0; i <= blocks; i++)
        DisconnectTip(state);

    return true;
}

/**
 * Return the tip of the chain with the most work in it, that isn't
 * known to be invalid (it's however far from certain to be valid).
 */
static CBlockIndex* FindMostWorkChain()
{
    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    do {
        CBlockIndex* pindexNew = NULL;

        // Find the best candidate header.
        {
            std::set<CBlockIndex*, CBlockIndexWorkComparator>::reverse_iterator it = setBlockIndexCandidates.rbegin();
            if (it == setBlockIndexCandidates.rend())
                return NULL;
            pindexNew = *it;
        }

        // Check whether all blocks on the path between the currently active chain and the candidate are valid.
        // Just going until the active chain is an optimization, as we know all blocks in it are valid already.
        CBlockIndex* pindexTest = pindexNew;
        bool fInvalidAncestor = false;
        while (pindexTest && !chain.Contains(pindexTest)) {
            assert(pindexTest->nChainTx || pindexTest->nHeight == 0);

            // Pruned nodes may have entries in setBlockIndexCandidates for
            // which block files have been deleted.  Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            bool fFailedChain = pindexTest->nStatus & BLOCK_FAILED_MASK;
            bool fMissingData = !(pindexTest->nStatus & BLOCK_HAVE_DATA);
            if (fFailedChain || fMissingData) {
                // Candidate chain is not usable (either invalid or missing data)
                if (fFailedChain && (pindexBestInvalid == NULL || pindexNew->nChainWork > pindexBestInvalid->nChainWork))
                    pindexBestInvalid = pindexNew;
                CBlockIndex* pindexFailed = pindexNew;
                // Remove the entire chain from the set.
                while (pindexTest != pindexFailed) {
                    if (fFailedChain) {
                        pindexFailed->nStatus |= BLOCK_FAILED_CHILD;
                    } else if (fMissingData) {
                        // If we're missing data, then add back to mapBlocksUnlinked,
                        // so that if the block arrives in the future we can try adding
                        // to setBlockIndexCandidates again.
                        mapBlocksUnlinked.insert(std::make_pair(pindexFailed->pprev, pindexFailed));
                    }
                    setBlockIndexCandidates.erase(pindexFailed);
                    pindexFailed = pindexFailed->pprev;
                }
                setBlockIndexCandidates.erase(pindexTest);
                fInvalidAncestor = true;
                break;
            }
            pindexTest = pindexTest->pprev;
        }
        if (!fInvalidAncestor)
            return pindexNew;
    } while (true);
}

/** Delete all entries in setBlockIndexCandidates that are worse than the current tip. */
static void PruneBlockIndexCandidates()
{
    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    auto it = setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() && setBlockIndexCandidates.value_comp()(*it, chain.Tip())) {
        setBlockIndexCandidates.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
}

/**
 * Try to make some progress towards making pindexMostWork the active block.
 * pblock is either NULL or a pointer to a CBlock corresponding to pindexMostWork.
 */
static bool ActivateBestChainStep(CValidationState& state, CBlockIndex* pindexMostWork, const CBlock* pblock, bool fAlreadyChecked)
{
    AssertLockHeld(cs_main);

    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    if (pblock == NULL)
        fAlreadyChecked = false;
    bool fInvalidFound = false;
    const CBlockIndex* pindexOldTip = chain.Tip();
    const CBlockIndex* pindexFork = chain.FindFork(pindexMostWork);

    // Disconnect active blocks which are no longer in the best chain.
    while (chain.Tip() && chain.Tip() != pindexFork) {
        if (!DisconnectTip(state))
            return false;
    }

    // Build list of new blocks to connect.
    std::vector<CBlockIndex*> vpindexToConnect;
    bool fContinue = true;
    int nHeight = pindexFork ? pindexFork->nHeight : -1;
    while (fContinue && nHeight != pindexMostWork->nHeight) {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        int nTargetHeight = std::min(nHeight + 32, pindexMostWork->nHeight);
        vpindexToConnect.clear();
        vpindexToConnect.reserve(nTargetHeight - nHeight);
        CBlockIndex* pindexIter = pindexMostWork->GetAncestor(nTargetHeight);
        while (pindexIter && pindexIter->nHeight != nHeight) {
            vpindexToConnect.push_back(pindexIter);
            pindexIter = pindexIter->pprev;
        }
        nHeight = nTargetHeight;

        // Connect new blocks.
        BOOST_REVERSE_FOREACH (CBlockIndex* pindexConnect, vpindexToConnect) {
            if (!ConnectTip(state, pindexConnect, pindexConnect == pindexMostWork ? pblock : NULL, fAlreadyChecked)) {
                if (state.IsInvalid()) {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                        InvalidChainFound(vpindexToConnect.back());
                    state = CValidationState();
                    fInvalidFound = true;
                    fContinue = false;
                    break;
                } else {
                    // A system error occurred (disk space, database error, ...).
                    return false;
                }
            } else {
                PruneBlockIndexCandidates();
                if (!pindexOldTip || chain.Tip()->nChainWork > pindexOldTip->nChainWork) {
                    // We're in a better position than we were. Return temporarily to release the lock.
                    fContinue = false;
                    break;
                }
            }
        }
    }

    // Callbacks/notifications for a new best chain.
    if (fInvalidFound)
        CheckForkWarningConditionsOnNewFork(vpindexToConnect.back());
    else
        CheckForkWarningConditions();

    return true;
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either NULL or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool ActivateBestChain(CValidationState& state, const CBlock* pblock, bool fAlreadyChecked)
{
    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    const CBlockIndex* pindexNewTip = NULL;
    CBlockIndex* pindexMostWork = NULL;
    do {
        boost::this_thread::interruption_point();

        bool fInitialDownload;
        while (true) {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) {
                MilliSleep(50);
                continue;
            }

            pindexMostWork = FindMostWorkChain();

            // Whether we have anything to do at all.
            if (pindexMostWork == NULL || pindexMostWork == chain.Tip())
                return true;

            if (!ActivateBestChainStep(state, pindexMostWork, pblock && pblock->GetHash() == pindexMostWork->GetBlockHash() ? pblock : NULL, fAlreadyChecked))
                return false;

            pindexNewTip = chain.Tip();
            fInitialDownload = IsInitialBlockDownload();
            break;
        }
        // When we reach this point, we switched to a new tip (stored in pindexNewTip).

        // Notifications/callbacks that can run without cs_main
        if (!fInitialDownload) {
            const uint256 hashNewTip = pindexNewTip->GetBlockHash();
            // Relay inventory, but don't relay old inventory during initial block download.
            int nBlockEstimate = checkpointsVerifier.GetTotalBlocksEstimate();
            NotifyPeersOfNewChainTip(chain.Height(),hashNewTip,nBlockEstimate);
            // Notify external listeners about the new tip.
            uiInterface.NotifyBlockTip(hashNewTip);
            g_signals.UpdatedBlockTip(pindexNewTip);
            timeOfLastChainTipUpdate = GetTime();
        }
    } while (pindexMostWork != chain.Tip());
    CheckBlockIndex();

    // Write changes periodically to disk, after relay.
    if (!FlushStateToDisk(state, FLUSH_STATE_PERIODIC)) {
        return false;
    }

    return true;
}

bool InvalidateBlock(CValidationState& state, CBlockIndex* pindex)
{
    AssertLockHeld(cs_main);

    ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();
    auto& blockMap = chainstate->GetBlockMap();

    // Mark the block itself as invalid.
    pindex->nStatus |= BLOCK_FAILED_VALID;
    BlockFileHelpers::RecordDirtyBlockIndex(pindex);
    setBlockIndexCandidates.erase(pindex);

    while (chain.Contains(pindex)) {
        CBlockIndex* pindexWalk = blockMap.at(chain.Tip()->GetBlockHash());
        pindexWalk->nStatus |= BLOCK_FAILED_CHILD;
        BlockFileHelpers::RecordDirtyBlockIndex(pindexWalk);
        setBlockIndexCandidates.erase(pindexWalk);
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(state)) {
            return false;
        }
    }

    // The resulting new best tip may not be in setBlockIndexCandidates anymore, so
    // add them again.
    for (const auto& entry : chainstate->GetBlockMap()) {
        if (entry.second->IsValid(BLOCK_VALID_TRANSACTIONS) && entry.second->nChainTx && !setBlockIndexCandidates.value_comp()(entry.second, chain.Tip())) {
            setBlockIndexCandidates.insert(entry.second);
        }
    }

    InvalidChainFound(pindex);
    return true;
}

bool ReconsiderBlock(CValidationState& state, CBlockIndex* pindex)
{
    AssertLockHeld(cs_main);

    ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    int nHeight = pindex->nHeight;

    // Remove the invalidity flag from this block and all its descendants.
    for (auto& entry : chainstate->GetBlockMap()) {
        CBlockIndex& blk = *entry.second;
        if (!blk.IsValid() && blk.GetAncestor(nHeight) == pindex) {
            blk.nStatus &= ~BLOCK_FAILED_MASK;
            BlockFileHelpers::RecordDirtyBlockIndex(&blk);
            if (blk.IsValid(BLOCK_VALID_TRANSACTIONS) && blk.nChainTx && setBlockIndexCandidates.value_comp()(chain.Tip(), &blk)) {
                setBlockIndexCandidates.insert(&blk);
            }
            if (&blk == pindexBestInvalid) {
                // Reset invalid block marker if it was pointing to one of those.
                pindexBestInvalid = NULL;
            }
        }
    }

    // Remove the invalidity flag from all ancestors too.
    while (pindex != NULL) {
        if (pindex->nStatus & BLOCK_FAILED_MASK) {
            pindex->nStatus &= ~BLOCK_FAILED_MASK;
            BlockFileHelpers::RecordDirtyBlockIndex(pindex);
        }
        pindex = pindex->pprev;
    }
    return true;
}

CBlockIndex* AddToBlockIndex(const CBlock& block)
{
    auto& chainstate = ChainstateManager::Get();
    auto& blockMap = chainstate.GetBlockMap();

    static const CSporkManager& sporkManager = GetSporkManager();
    static BlockIndexLotteryUpdater lotteryUpdater(Params(), chainstate.ActiveChain(), sporkManager);
    // Check for duplicate
    const uint256 hash = block.GetHash();
    const auto it = blockMap.find(hash);
    if (it != blockMap.end())
        return it->second;

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(block);
    assert(pindexNew);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    const auto mi = blockMap.insert(std::make_pair(hash, pindexNew)).first;

    pindexNew->phashBlock = &((*mi).first);
    const auto miPrev = blockMap.find(block.hashPrevBlock);
    if (miPrev != blockMap.end()) {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        pindexNew->BuildSkip();

        //update previous block pointer
        pindexNew->pprev->pnext = pindexNew;

        // ppcoin: compute chain trust score
        pindexNew->bnChainTrust = (pindexNew->pprev ? pindexNew->pprev->bnChainTrust : 0) + pindexNew->GetBlockTrust();

        // ppcoin: compute stake entropy bit for stake modifier
        if (!pindexNew->SetStakeEntropyBit(pindexNew->GetStakeEntropyBit()))
            LogPrintf("AddToBlockIndex() : SetStakeEntropyBit() failed \n");

        // ppcoin: record proof-of-stake hash value
        if (pindexNew->IsProofOfStake()) {
            if (!mapProofOfStake.count(hash))
                LogPrintf("AddToBlockIndex() : hashProofOfStake not found in map \n");
            pindexNew->hashProofOfStake = mapProofOfStake[hash];
        }

        // ppcoin: compute stake modifier
        SetStakeModifiersForNewBlockIndex(blockMap, pindexNew);
    }
    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);
    if (pindexBestHeader == NULL || pindexBestHeader->nChainWork < pindexNew->nChainWork)
        pindexBestHeader = pindexNew;

    //update previous block pointer
    if (pindexNew->nHeight)
        pindexNew->pprev->pnext = pindexNew;

    lotteryUpdater.UpdateBlockIndexLotteryWinners(block,pindexNew);

    BlockFileHelpers::RecordDirtyBlockIndex(pindexNew);

    return pindexNew;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
bool ReceivedBlockTransactions(const CBlock& block, CValidationState& state, CBlockIndex* pindexNew, const CDiskBlockPos& pos)
{
    if (block.IsProofOfStake())
        pindexNew->SetProofOfStake();
    pindexNew->nTx = block.vtx.size();
    pindexNew->nChainTx = 0;
    pindexNew->nFile = pos.nFile;
    pindexNew->nDataPos = pos.nPos;
    pindexNew->nUndoPos = 0;
    pindexNew->nStatus |= BLOCK_HAVE_DATA;
    pindexNew->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
    BlockFileHelpers::RecordDirtyBlockIndex(pindexNew);

    if (pindexNew->pprev == NULL || pindexNew->pprev->nChainTx) {
        const ChainstateManager::Reference chainstate;
        const auto& chain = chainstate->ActiveChain();

        // If pindexNew is the genesis block or all parents are BLOCK_VALID_TRANSACTIONS.
        deque<CBlockIndex*> queue;
        queue.push_back(pindexNew);

        // Recursively process any descendant blocks that now may be eligible to be connected.
        while (!queue.empty()) {
            CBlockIndex* pindex = queue.front();
            queue.pop_front();
            pindex->nChainTx = (pindex->pprev ? pindex->pprev->nChainTx : 0) + pindex->nTx;
            {
                LOCK(cs_nBlockSequenceId);
                pindex->nSequenceId = nBlockSequenceId++;
            }
            if (chain.Tip() == NULL || !setBlockIndexCandidates.value_comp()(pindex, chain.Tip())) {
                setBlockIndexCandidates.insert(pindex);
            }
            auto range = mapBlocksUnlinked.equal_range(pindex);
            while (range.first != range.second) {
                auto it = range.first;
                queue.push_back(it->second);
                range.first++;
                mapBlocksUnlinked.erase(it);
            }
        }
    } else {
        if (pindexNew->pprev && pindexNew->pprev->IsValid(BLOCK_VALID_TREE)) {
            mapBlocksUnlinked.insert(std::make_pair(pindexNew->pprev, pindexNew));
        }
    }

    return true;
}

bool CheckBlock(const CBlock& block, CValidationState& state)
{
    // These are checks that are independent of context.

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (block.IsProofOfWork() && !CheckProofOfWork(block.GetHash(), block.nBits, Params()))
    {
        return state.DoS(150, error("%s : proof of work failed",__func__),
                         REJECT_INVALID, "bad-header: high-hash", true);
    }

    // Check timestamp
    LogPrint("debug", "%s: block=%s  is proof of stake=%d\n", __func__, block.GetHash(), block.IsProofOfStake());
    if (block.GetBlockTime() > GetAdjustedTime() + (block.IsProofOfStake() ? settings.MaxFutureBlockDrift() : 7200)) // 3 minute future drift for PoS
        return state.Invalid(error("%s : block timestamp too far in the future",__func__),
                             REJECT_INVALID, "time-too-new");

    // Check the merkle root.
    bool mutated;
    uint256 hashMerkleRoot2 = block.BuildMerkleTree(&mutated);
    if (block.hashMerkleRoot != hashMerkleRoot2)
        return state.DoS(100, error("%s : hashMerkleRoot mismatch",__func__),
                         REJECT_INVALID, "bad-txnmrklroot", true);

    // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
    // of transactions in a block without affecting the merkle root of a block,
    // while still invalidating it.
    if (mutated)
        return state.DoS(100, error("%s : duplicate transaction",__func__),
                         REJECT_INVALID, "bad-txns-duplicate", true);

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.

    // Size limits
    if (block.vtx.empty() || block.vtx.size() > MAX_BLOCK_SIZE_CURRENT || ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE_CURRENT)
        return state.DoS(100, error("%s : size limits failed",__func__),
                         REJECT_INVALID, "bad-blk-length");

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0].IsCoinBase())
        return state.DoS(100, error("%s : first tx is not coinbase",__func__),
                         REJECT_INVALID, "bad-cb-missing");
    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i].IsCoinBase())
            return state.DoS(100, error("%s : more than one coinbase",__func__),
                             REJECT_INVALID, "bad-cb-multiple");

    if (block.IsProofOfStake()) {
        // Coinbase output should be empty if proof-of-stake block
        if (block.vtx[0].vout.size() != 1 || !block.vtx[0].vout[0].IsEmpty())
            return state.DoS(100, error("%s : coinbase output not empty for proof-of-stake block",__func__));

        // Second transaction must be coinstake, the rest must not be
        if (block.vtx.empty() || !block.vtx[1].IsCoinStake())
            return state.DoS(100, error("%s : second tx is not coinstake",__func__));
        for (unsigned int i = 2; i < block.vtx.size(); i++)
            if (block.vtx[i].IsCoinStake())
                return state.DoS(100, error("%s : more than one coinstake",__func__));
    }

    // Check transactions
    std::set<COutPoint> inputsUsedByBlockTransactions;
    for (const CTransaction& tx : block.vtx) {
        if (!CheckTransaction(tx, state,inputsUsedByBlockTransactions))
            return error("%s : CheckTransaction failed",__func__);
    }


    unsigned int nSigOps = 0;
    for(const CTransaction& tx: block.vtx) {
        nSigOps += GetLegacySigOpCount(tx);
    }
    unsigned int nMaxBlockSigOps = MAX_BLOCK_SIGOPS_LEGACY;
    if (nSigOps > nMaxBlockSigOps)
        return state.DoS(100, error("%s : out-of-bounds SigOpCount",__func__),
                         REJECT_INVALID, "bad-blk-sigops", true);

    return true;
}

bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex* const pindexPrev)
{
    const uint256 hash = block.GetHash();

    if (hash == Params().HashGenesisBlock())
        return true;

    assert(pindexPrev);

    const ChainstateManager::Reference chainstate;
    int nHeight = pindexPrev->nHeight + 1;

    //If this is a reorg, check that it is not too deep
    int nMaxReorgDepth = settings.GetArg("-maxreorg", Params().MaxReorganizationDepth());
    if (chainstate->ActiveChain().Height() - nHeight >= nMaxReorgDepth)
        return state.DoS(1, error("%s: forked chain older than max reorganization depth (height %d)", __func__, nHeight));

    // Check timestamp against prev
    if (block.GetBlockTime() <= pindexPrev->GetMedianTimePast()) {
        LogPrintf("Block time = %d , GetMedianTimePast = %d \n", block.GetBlockTime(), pindexPrev->GetMedianTimePast());
        return state.Invalid(error("%s : block's timestamp is too early", __func__),
                             REJECT_INVALID, "time-too-old");
    }

    // Check that the block chain matches the known block chain up to a checkpoint
    if (!checkpointsVerifier.CheckBlock(nHeight, hash))
        return state.DoS(100, error("%s : rejected by checkpoint lock-in at %d", __func__, nHeight),
                         REJECT_CHECKPOINT, "checkpoint mismatch");

    // Don't accept any forks from the main chain prior to last checkpoint
    CBlockIndex* pcheckpoint = checkpointsVerifier.GetLastCheckpoint(chainstate->GetBlockMap());
    if (pcheckpoint && nHeight < pcheckpoint->nHeight)
        return state.DoS(0, error("%s : forked chain older than last checkpoint (height %d)", __func__, nHeight));

    // All blocks on DIVI later than the genesis block must be at least version 3.
    // (In fact they are version 4, but we only enforce version >=3 as this is what
    // the previous check based on BIP34 supermajority activation did.)
    if (block.nVersion < 3)
        return state.Invalid(error("%s : rejected nVersion=%d block", __func__, block.nVersion),
                             REJECT_OBSOLETE, "bad-version");

    return true;
}

bool ContextualCheckBlock(const CBlock& block, CValidationState& state, CBlockIndex* const pindexPrev)
{
    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    const int nHeight = pindexPrev == NULL ? 0 : pindexPrev->nHeight + 1;

    // Check that all transactions are finalized
    for (const auto& tx : block.vtx)
            if (!IsFinalTx(tx, chain, nHeight, block.GetBlockTime())) {
        return state.DoS(10, error("%s : contains a non-final transaction", __func__), REJECT_INVALID, "bad-txns-nonfinal");
    }

    // Enforce the BIP34 rule that the coinbase starts with serialized block height.
    // The genesis block is the only exception.
    if (nHeight > 0) {
        const CScript expect = CScript() << nHeight;
        if (block.vtx[0].vin[0].scriptSig.size() < expect.size() ||
                !std::equal(expect.begin(), expect.end(), block.vtx[0].vin[0].scriptSig.begin())) {
            return state.DoS(100, error("%s : block height mismatch in coinbase", __func__), REJECT_INVALID, "bad-cb-height");
        }
    }

    return true;
}

bool AcceptBlockHeader(const CBlock& block, CValidationState& state, CBlockIndex** ppindex)
{
    AssertLockHeld(cs_main);

    const ChainstateManager::Reference chainstate;
    const auto& blockMap = chainstate->GetBlockMap();

    // Check for duplicate
    uint256 hash = block.GetHash();
    const auto miSelf = blockMap.find(hash);
    CBlockIndex* pindex = NULL;

    // TODO : ENABLE BLOCK CACHE IN SPECIFIC CASES
    if (miSelf != blockMap.end()) {
        // Block header is already known.
        pindex = miSelf->second;
        if (ppindex)
            *ppindex = pindex;
        if (pindex->nStatus & BLOCK_FAILED_MASK)
            return state.Invalid(error("%s : block is marked invalid", __func__), 0, "duplicate");
        return true;
    }

    // Get prev block index
    CBlockIndex* pindexPrev = NULL;
    if (hash != Params().HashGenesisBlock()) {
        const auto mi = blockMap.find(block.hashPrevBlock);
        if (mi == blockMap.end())
            return state.DoS(0, error("%s : prev block %s not found", __func__, block.hashPrevBlock), 0, "bad-prevblk");
        pindexPrev = (*mi).second;
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK) {
            //If this "invalid" block is an exact match from the checkpoints, then reconsider it
            if (pindex && checkpointsVerifier.CheckBlock(pindex->nHeight - 1, block.hashPrevBlock, true)) {
                LogPrintf("%s : Reconsidering block %s height %d\n", __func__, pindexPrev->GetBlockHash(), pindexPrev->nHeight);
                CValidationState statePrev;
                ReconsiderBlock(statePrev, pindexPrev);
                if (statePrev.IsValid()) {
                    ActivateBestChain(statePrev);
                    return true;
                }
            }

            return state.DoS(100, error("%s : prev block height=%d hash=%s is invalid, unable to add block %s", __func__, pindexPrev->nHeight, block.hashPrevBlock, block.GetHash()),
                             REJECT_INVALID, "bad-prevblk");
        }

    }

    if (!ContextualCheckBlockHeader(block, state, pindexPrev))
        return false;

    if (pindex == NULL)
        pindex = AddToBlockIndex(block);

    if (ppindex)
        *ppindex = pindex;

    return true;
}

bool AcceptBlock(CBlock& block, CValidationState& state, CBlockIndex** ppindex, CDiskBlockPos* dbp, bool fAlreadyCheckedBlock)
{
    AssertLockHeld(cs_main);

    const auto& chainstate = ChainstateManager::Get();
    const auto& blockMap = chainstate.GetBlockMap();

    CBlockIndex*& pindex = *ppindex;

    // Get prev block index
    CBlockIndex* pindexPrev = NULL;
    if (block.GetHash() != Params().HashGenesisBlock()) {
        const auto mi = blockMap.find(block.hashPrevBlock);
        if (mi == blockMap.end())
            return state.DoS(0, error("%s : prev block %s not found", __func__, block.hashPrevBlock), 0, "bad-prevblk");
        pindexPrev = (*mi).second;
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK) {
            //If this "invalid" block is an exact match from the checkpoints, then reconsider it
            if (checkpointsVerifier.CheckBlock(pindexPrev->nHeight, block.hashPrevBlock, true)) {
                LogPrintf("%s : Reconsidering block %s height %d\n", __func__, pindexPrev->GetBlockHash(), pindexPrev->nHeight);
                CValidationState statePrev;
                ReconsiderBlock(statePrev, pindexPrev);
                if (statePrev.IsValid()) {
                    ActivateBestChain(statePrev);
                    return true;
                }
            }
            return state.DoS(100, error("%s : prev block %s is invalid, unable to add block %s", __func__, block.hashPrevBlock, block.GetHash()),
                             REJECT_INVALID, "bad-prevblk");
        }
    }

    static ProofOfStakeModule posModule(Params(), chainstate.ActiveChain(), blockMap);
    static const I_ProofOfStakeGenerator& posGenerator = posModule.proofOfStakeGenerator();

    const uint256 blockHash = block.GetHash();
    if (blockHash != Params().HashGenesisBlock())
    {
        if(!CheckWork(Params(), posGenerator, blockMap, settings, block, mapProofOfStake, pindexPrev))
        {
            LogPrintf("WARNING: %s: check difficulty check failed for %s block %s\n",__func__, block.IsProofOfWork()?"PoW":"PoS", blockHash);
            return false;
        }
    }

    if (!AcceptBlockHeader(block, state, &pindex))
        return false;

    if (pindex->nStatus & BLOCK_HAVE_DATA) {
        // TODO: deal better with duplicate blocks.
        // return state.DoS(20, error("AcceptBlock() : already have block %d %s", pindex->nHeight, pindex->GetBlockHash()), REJECT_DUPLICATE, "duplicate");
        return true;
    }

    if ((!fAlreadyCheckedBlock && !CheckBlock(block, state)) || !ContextualCheckBlock(block, state, pindex->pprev)) {
        if (state.IsInvalid() && !state.CorruptionPossible()) {
            pindex->nStatus |= BLOCK_FAILED_VALID;
            BlockFileHelpers::RecordDirtyBlockIndex(pindex);
        }
        return false;
    }

    int nHeight = pindex->nHeight;

    // Write block to history file
    try {
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        if (dbp != NULL)
            blockPos = *dbp;
        if (!FindBlockPos(state, blockPos, nBlockSize + 8, nHeight, block.GetBlockTime(), dbp != NULL))
            return error("AcceptBlock() : FindBlockPos failed");
        if (dbp == NULL)
            if (!WriteBlockToDisk(block, blockPos))
                return state.Abort("Failed to write block");
        if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
            return error("AcceptBlock() : ReceivedBlockTransactions failed");
    } catch (std::runtime_error& e) {
        return state.Abort(std::string("System error: ") + e.what());
    }

    return true;
}

bool ProcessNewBlock(CValidationState& state, CNode* pfrom, CBlock* pblock, CDiskBlockPos* dbp)
{
    // Preliminary checks
    int64_t nStartTime = GetTimeMillis();
    bool checked = CheckBlock(*pblock, state);

    // NovaCoin: check proof-of-stake block signature
    if (!CheckBlockSignature(*pblock))
        return error("%s : bad proof-of-stake block signature",__func__);

    const ChainstateManager::Reference chainstate;
    const auto& blockMap = chainstate->GetBlockMap();

    if (pblock->GetHash() != Params().HashGenesisBlock() && pfrom != NULL) {
        //if we get this far, check if the prev block is our prev block, if not then request sync and return false
        const auto mi = blockMap.find(pblock->hashPrevBlock);
        if (mi == blockMap.end()) {
            pfrom->PushMessage("getblocks", chainstate->ActiveChain().GetLocator(), uint256(0));
            return false;
        }
    }

    CBlockIndex* pindex = nullptr;
    {
        LOCK(cs_main);   // Replaces the former TRY_LOCK loop because busy waiting wastes too much resources

        MarkBlockAsReceived (pblock->GetHash ());
        if (!checked) {
            return error ("%s : CheckBlock FAILED for block %s", __func__, pblock->GetHash());
        }

        // Store to disk
        bool ret = AcceptBlock(*pblock, state, &pindex, dbp, checked);
        if (pindex && pfrom) {
            mapBlockSource[pindex->GetBlockHash ()] = pfrom->GetId ();
        }
        CheckBlockIndex ();
        if (!ret)
            return error ("%s : AcceptBlock FAILED", __func__);
    }
    assert(pindex != nullptr);

    if (!ActivateBestChain(state, pblock, checked))
        return error("%s : ActivateBestChain failed", __func__);

    VoteForMasternodePayee(pindex);
    LogPrintf("%s : ACCEPTED in %ld milliseconds with size=%d\n", __func__, GetTimeMillis() - nStartTime,
              pblock->GetSerializeSize(SER_DISK, CLIENT_VERSION));

    return true;
}
bool IsBlockValidChainExtension(CBlock* pblock)
{
    {
        LOCK(cs_main);
        const ChainstateManager::Reference chainstate;
        if (pblock->hashPrevBlock != chainstate->ActiveChain().Tip()->GetBlockHash())
            return error("%s : generated block is stale",__func__);
    }
    return true;
}
bool ProcessNewBlockFoundByMe(CBlock* pblock)
{
    LogPrintf("%s\n", *pblock);
    LogPrintf("generated %s\n", FormatMoney(pblock->vtx[0].vout[0].nValue));

    // Process this block the same as if we had received it from another node
    CValidationState state;
    if (!IsBlockValidChainExtension(pblock) || !ProcessNewBlock(state, NULL, pblock))
        return error("%s : block not accepted",__func__);

    return true;
}

static std::vector<std::pair<int, CBlockIndex*> > ComputeHeightSortedBlockIndices(BlockMap& blockIndicesByHash)
{
    std::vector<std::pair<int, CBlockIndex*> > heightSortedBlockIndices;
    heightSortedBlockIndices.reserve(blockIndicesByHash.size());
    for (const auto& item : blockIndicesByHash) {
        CBlockIndex* pindex = item.second;
        heightSortedBlockIndices.push_back(make_pair(pindex->nHeight, pindex));
    }
    std::sort(heightSortedBlockIndices.begin(), heightSortedBlockIndices.end());
    return heightSortedBlockIndices;
}

static void InitializeBlockIndexGlobalData(const std::vector<std::pair<int, CBlockIndex*> >& heightSortedBlockIndices)
{
    for(const PAIRTYPE(int, CBlockIndex*) & item: heightSortedBlockIndices)
    {
        CBlockIndex* pindex = item.second;
        pindex->nChainWork = (pindex->pprev ? pindex->pprev->nChainWork : 0) + GetBlockProof(*pindex);
        if (pindex->nStatus & BLOCK_HAVE_DATA) {
            if (pindex->pprev) {
                if (pindex->pprev->nChainTx) {
                    pindex->nChainTx = pindex->pprev->nChainTx + pindex->nTx;
                } else {
                    pindex->nChainTx = 0;
                    mapBlocksUnlinked.insert(std::make_pair(pindex->pprev, pindex));
                }
            } else {
                pindex->nChainTx = pindex->nTx;
            }
        }
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && (pindex->nChainTx || pindex->pprev == NULL))
            setBlockIndexCandidates.insert(pindex);
        if (pindex->nStatus & BLOCK_FAILED_MASK && (!pindexBestInvalid || pindex->nChainWork > pindexBestInvalid->nChainWork))
            pindexBestInvalid = pindex;
        if (pindex->pprev){
            pindex->BuildSkip();
            CBlockIndex* pAncestor = pindex->GetAncestor(pindex->vLotteryWinnersCoinstakes.height());
            pindex->vLotteryWinnersCoinstakes.updateShallowDataStore(pAncestor->vLotteryWinnersCoinstakes);
        }
        if (pindex->IsValid(BLOCK_VALID_TREE) && (pindexBestHeader == NULL || CBlockIndexWorkComparator()(pindexBestHeader, pindex)))
            pindexBestHeader = pindex;
    }
}
static bool VerifyAllBlockFilesArePresent(const BlockMap& blockIndicesByHash)
{
    LogPrintf("Checking all blk files are present...\n");
    std::set<int> setBlkDataFiles;
    for (const auto& item : blockIndicesByHash) {
        CBlockIndex* pindex = item.second;
        if (pindex->nStatus & BLOCK_HAVE_DATA) {
            setBlkDataFiles.insert(pindex->nFile);
        }
    }
    for (int blockFileNumber: setBlkDataFiles)
    {
        CDiskBlockPos pos(blockFileNumber, 0);
        if (CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull()) {
            return false;
        }
    }
    return true;
}

bool static LoadBlockIndexState(string& strError)
{
    ChainstateManager::Reference chainstate;
    auto& chain = chainstate->ActiveChain();
    auto& coinsTip = chainstate->CoinsTip();
    auto& blockMap = chainstate->GetBlockMap();
    auto& blockTree = chainstate->BlockTree();

    if (!blockTree.LoadBlockIndices(blockMap))
        return error("Failed to load block indices from database");

    boost::this_thread::interruption_point();

    // Calculate nChainWork
    std::vector<std::pair<int, CBlockIndex*> > heightSortedBlockIndices = ComputeHeightSortedBlockIndices(blockMap);
    InitializeBlockIndexGlobalData(heightSortedBlockIndices);

    // Load block file info
    BlockFileHelpers::ReadBlockFiles(blockTree);

    // Check presence of blk files
    if(!VerifyAllBlockFilesArePresent(blockMap)) return error("Some block files that were expected to be found are missing!");

    //Check if the shutdown procedure was followed on last client exit
    bool fLastShutdownWasPrepared = true;
    blockTree.ReadFlag("shutdown", fLastShutdownWasPrepared);
    LogPrintf("%s: Last shutdown was prepared: %s\n", __func__, fLastShutdownWasPrepared);

    //Check for inconsistency with block file info and internal state
    if (!fLastShutdownWasPrepared && !settings.GetBoolArg("-forcestart", false) && !settings.GetBoolArg("-reindex", false))
    {
        const unsigned int expectedNumberOfBlockIndices = BlockFileHelpers::GetLastBlockHeightWrittenIntoLastBlockFile() + 1;
        if (heightSortedBlockIndices.size() > expectedNumberOfBlockIndices)
        {
            const auto mit = blockMap.find(coinsTip.GetBestBlock());
            if (mit == blockMap.end())
            {
                strError = "The wallet has been not been closed gracefully, causing the transaction database to be out of sync with the block database";
                return false;
            }
            const int coinsHeight = mit->second->nHeight;
            const int blockIndexHeight = (heightSortedBlockIndices.size()>0)? heightSortedBlockIndices.back().first: 0;
            LogPrintf("%s : pcoinstip synced to block height %d, block index height %d\n", __func__, coinsHeight, blockIndexHeight);
            assert(coinsHeight <= blockIndexHeight);
            if(coinsHeight < blockIndexHeight)
            {
                //The database is in a state where a block has been accepted and written to disk, but the
                //transaction database (pcoinsTip) was not flushed to disk, and is therefore not in sync with
                //the block index database.
                auto nextBlockCandidate =
                    std::find_if(heightSortedBlockIndices.begin(),heightSortedBlockIndices.end(),
                        [coinsHeight](const std::pair<int,CBlockIndex*>& heightAndBlockIndex) -> bool
                        {
                            return heightAndBlockIndex.first > coinsHeight;
                        });
                auto endNextBlockCandidate = heightSortedBlockIndices.end();

                // Start at the last block that was successfully added to the txdb (pcoinsTip) and manually add all transactions that occurred for each block up until
                // the best known block from the block index db.
                CCoinsViewCache view(&coinsTip);
                int lastProcessedHeight = -1;
                while (nextBlockCandidate != endNextBlockCandidate)
                {
                    const CBlockIndex* pindex = nextBlockCandidate->second;
                    if(pindex->pprev && view.GetBestBlock() != pindex->pprev->GetBlockHash())
                    {
                        ++nextBlockCandidate;
                        continue;
                    }
                    if(lastProcessedHeight==pindex->nHeight)
                    {
                        // Duplicate blocks at the same height let the rest sort themselves out?
                        break;
                    }
                    lastProcessedHeight = pindex->nHeight;
                    CBlock block;
                    if (!ReadBlockFromDisk(block, pindex)) {
                        strError = "The wallet has been not been closed gracefully and has caused corruption of blocks stored to disk. Data directory is in an unusable state";
                        return false;
                    }

                    uint256 hashBlock = block.GetHash();
                    for (unsigned int i = 0; i < block.vtx.size(); i++) {
                        CTxUndo undoDummy;
                        view.UpdateWithConfirmedTransaction(block.vtx[i], pindex->nHeight, undoDummy);
                        view.SetBestBlock(hashBlock);
                    }
                    ++nextBlockCandidate;
                }

                // Save the updates to disk
                if (!view.Flush() || !coinsTip.Flush())
                    LogPrintf("%s : failed to flush view\n", __func__);

                //get the index associated with the point in the chain that pcoinsTip is synced to
                const CBlockIndex* const pindexLastMeta = heightSortedBlockIndices[expectedNumberOfBlockIndices].second;
                LogPrintf("%s: Last block properly recorded: #%d %s\n", __func__, pindexLastMeta->nHeight,
                        pindexLastMeta->GetBlockHash());
                LogPrintf("%s : pcoinstip=%d %s\n", __func__, coinsHeight, coinsTip.GetBestBlock());
            }
        }
    }

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    blockTree.ReadReindexing(fReindexing);
    fReindex |= fReindexing;

    // Check whether we have a transaction index
    blockTree.ReadFlag("txindex", fTxIndex);
    LogPrintf("%s: transaction index %s\n",__func__, fTxIndex ? "enabled" : "disabled");

    // Check whether we have an address index
    blockTree.ReadFlag("addressindex", fAddressIndex);
    LogPrintf("%s: address index %s\n", __func__, fAddressIndex ? "enabled" : "disabled");

    // Check whether we have a spent index
    blockTree.ReadFlag("spentindex", fSpentIndex);
    LogPrintf("%s: spent index %s\n", __func__, fSpentIndex ? "enabled" : "disabled");

    // If this is written true before the next client init, then we know the shutdown process failed
    blockTree.WriteFlag("shutdown", false);

    // Load pointer to end of best chain
    const auto it = blockMap.find(coinsTip.GetBestBlock());
    if (it == blockMap.end())
        return true;
    chain.SetTip(it->second);

    PruneBlockIndexCandidates();

    LogPrintf("%s: hashBestChain=%s height=%d date=%s progress=%f\n",
            __func__,
            chain.Tip()->GetBlockHash(), chain.Height(),
            DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chain.Tip()->GetBlockTime()),
            checkpointsVerifier.GuessVerificationProgress(chain.Tip()));

    return true;
}

void UnloadBlockIndex(ChainstateManager* chainstate)
{
    setBlockIndexCandidates.clear();
    pindexBestInvalid = nullptr;

    if (chainstate != nullptr)
    {
        auto& blockMap = chainstate->GetBlockMap();

        for(auto& blockHashAndBlockIndex: blockMap)
        {
            delete blockHashAndBlockIndex.second;
        }
        blockMap.clear();
        chainstate->ActiveChain().SetTip(nullptr);
    }
}

bool LoadBlockIndex(string& strError)
{
    // Load block index from databases
    if (!fReindex && !LoadBlockIndexState(strError))
        return false;
    return true;
}


bool InitBlockIndex()
{
    LOCK(cs_main);

    ChainstateManager::Reference chainstate;
    auto& blockTree = chainstate->BlockTree();

    // Check whether we're already initialized
    if (chainstate->ActiveChain().Genesis() != nullptr)
        return true;

    // Use the provided setting for -txindex in the new database
    fTxIndex = settings.GetBoolArg("-txindex", true);
    blockTree.WriteFlag("txindex", fTxIndex);

    // Use the provided setting for -addressindex in the new database
    fAddressIndex = settings.GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX);
    blockTree.WriteFlag("addressindex", fAddressIndex);

    fSpentIndex = settings.GetBoolArg("-spentindex", DEFAULT_SPENTINDEX);
    blockTree.WriteFlag("spentindex", fSpentIndex);

    LogPrintf("Initializing databases...\n");

    // Only add the genesis block if not reindexing (in which case we reuse the one already on disk)
    if (!fReindex) {
        try {
            const CBlock& block = Params().GenesisBlock();
            // Start new block file
            unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
            CDiskBlockPos blockPos;
            CValidationState state;
            if (!FindBlockPos(state, blockPos, nBlockSize + 8, 0, block.GetBlockTime()))
                return error("LoadBlockIndex() : FindBlockPos failed");
            if (!WriteBlockToDisk(block, blockPos))
                return error("LoadBlockIndex() : writing genesis block to disk failed");
            CBlockIndex* pindex = AddToBlockIndex(block);
            if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
                return error("LoadBlockIndex() : genesis block not accepted");
            if (!ActivateBestChain(state, &block))
                return error("LoadBlockIndex() : genesis block cannot be activated");
            // Force a chainstate write so that when we VerifyDB in a moment, it doesnt check stale data
            return FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
        } catch (std::runtime_error& e) {
            return error("LoadBlockIndex() : failed to initialize block database: %s", e.what());
        }
    }
    return true;
}


bool LoadExternalBlockFile(FILE* fileIn, CDiskBlockPos* dbp)
{
    // Map of disk positions for blocks with unknown parent (only used for reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    const ChainstateManager::Reference chainstate;
    const auto& blockMap = chainstate->GetBlockMap();

    int nLoaded = 0;
    try {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        CBufferedFile blkdat(fileIn, 2 * MAX_BLOCK_SIZE_CURRENT, MAX_BLOCK_SIZE_CURRENT + 8, SER_DISK, CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof()) {
            boost::this_thread::interruption_point();

            blkdat.SetPos(nRewind);
            nRewind++;         // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try {
                // locate a header
                unsigned char buf[MESSAGE_START_SIZE];
                blkdat.FindByte(Params().MessageStart()[0]);
                nRewind = blkdat.GetPos() + 1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, Params().MessageStart(), MESSAGE_START_SIZE))
                    continue;
                // read size
                blkdat >> nSize;
                if (nSize < 80 || nSize > MAX_BLOCK_SIZE_CURRENT)
                    continue;
            } catch (const std::exception&) {
                // no valid block header found; don't complain
                break;
            }
            try {
                // read block
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                CBlock block;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                // detect out of order blocks, and store them for later
                uint256 hash = block.GetHash();
                if (hash != Params().HashGenesisBlock() && blockMap.count(block.hashPrevBlock) == 0) {
                    LogPrint("reindex", "%s: Out of order block %s, parent %s not known\n", __func__, hash,
                             block.hashPrevBlock);
                    if (dbp)
                        mapBlocksUnknownParent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                    continue;
                }

                // process in case the block isn't known yet
                const auto mit = blockMap.find(hash);
                if (mit == blockMap.end() || (mit->second->nStatus & BLOCK_HAVE_DATA) == 0) {
                    CValidationState state;
                    if (ProcessNewBlock(state, NULL, &block, dbp))
                        nLoaded++;
                    if (state.IsError())
                        break;
                } else if (hash != Params().HashGenesisBlock() && mit->second->nHeight % 1000 == 0) {
                    LogPrintf("Block Import: already had block %s at height %d\n", hash, mit->second->nHeight);
                }

                // Recursively process earlier encountered successors of this block
                deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty()) {
                    uint256 head = queue.front();
                    queue.pop_front();
                    std::pair<std::multimap<uint256, CDiskBlockPos>::iterator, std::multimap<uint256, CDiskBlockPos>::iterator> range = mapBlocksUnknownParent.equal_range(head);
                    while (range.first != range.second) {
                        std::multimap<uint256, CDiskBlockPos>::iterator it = range.first;
                        if (ReadBlockFromDisk(block, it->second)) {
                            LogPrintf("%s: Processing out of order child %s of %s\n", __func__, block.GetHash(), head);
                            CValidationState dummy;
                            if (ProcessNewBlock(dummy, NULL, &block, &it->second)) {
                                nLoaded++;
                                queue.push_back(block.GetHash());
                            }
                        }
                        range.first++;
                        mapBlocksUnknownParent.erase(it);
                    }
                }
            } catch (std::exception& e) {
                LogPrintf("%s : Deserialize or I/O error - %s", __func__, e.what());
            }
        }
    } catch (std::runtime_error& e) {
        CValidationState().Abort(std::string("System error: ") + e.what());
    }
    if (nLoaded > 0)
        LogPrintf("Loaded %i blocks from external file in %dms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

void static CheckBlockIndex()
{
    if (!fCheckBlockIndex) {
        return;
    }

    LOCK(cs_main);

    const ChainstateManager::Reference chainstate;
    const auto& blockMap = chainstate->GetBlockMap();
    const auto& chain = chainstate->ActiveChain();

    // During a reindex, we read the genesis block and call CheckBlockIndex before ActivateBestChain,
    // so we have the genesis block in mapBlockIndex but no active chain.  (A few of the tests when
    // iterating the block tree require that chainActive has been initialized.)
    if (chain.Height() < 0) {
        assert(blockMap.size() <= 1);
        return;
    }

    // Build forward-pointing map of the entire block tree.
    std::multimap<CBlockIndex*, CBlockIndex*> forward;
    for (const auto& entry : blockMap) {
        forward.insert(std::make_pair(entry.second->pprev, entry.second));
    }

    assert(forward.size() == blockMap.size());

    std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> rangeGenesis = forward.equal_range(NULL);
    CBlockIndex* pindex = rangeGenesis.first->second;
    rangeGenesis.first++;
    assert(rangeGenesis.first == rangeGenesis.second); // There is only one index entry with parent NULL.

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int nHeight = 0;
    CBlockIndex* pindexFirstInvalid = NULL;         // Oldest ancestor of pindex which is invalid.
    CBlockIndex* pindexFirstMissing = NULL;         // Oldest ancestor of pindex which does not have BLOCK_HAVE_DATA.
    CBlockIndex* pindexFirstNotTreeValid = NULL;    // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE (regardless of being valid or not).
    CBlockIndex* pindexFirstNotChainValid = NULL;   // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN (regardless of being valid or not).
    CBlockIndex* pindexFirstNotScriptsValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS (regardless of being valid or not).
    while (pindex != NULL) {
        nNodes++;
        if (pindexFirstInvalid == NULL && pindex->nStatus & BLOCK_FAILED_VALID) pindexFirstInvalid = pindex;
        if (pindexFirstMissing == NULL && !(pindex->nStatus & BLOCK_HAVE_DATA)) pindexFirstMissing = pindex;
        if (pindex->pprev != NULL && pindexFirstNotTreeValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE) pindexFirstNotTreeValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotChainValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN) pindexFirstNotChainValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotScriptsValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS) pindexFirstNotScriptsValid = pindex;

        // Begin: actual consistency checks.
        if (pindex->pprev == NULL) {
            // Genesis block checks.
            assert(pindex->GetBlockHash() == Params().HashGenesisBlock()); // Genesis block's hash must match.
            assert(pindex == chain.Genesis());                       // The current active chain's genesis block must be this block.
        }
        // HAVE_DATA is equivalent to VALID_TRANSACTIONS and equivalent to nTx > 0 (we stored the number of transactions in the block)
        assert(!(pindex->nStatus & BLOCK_HAVE_DATA) == (pindex->nTx == 0));
        assert(((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TRANSACTIONS) == (pindex->nTx > 0));
        if (pindex->nChainTx == 0) assert(pindex->nSequenceId == 0); // nSequenceId can't be set for blocks that aren't linked
        // All parents having data is equivalent to all parents being VALID_TRANSACTIONS, which is equivalent to nChainTx being set.
        assert((pindexFirstMissing != NULL) == (pindex->nChainTx == 0));                                             // nChainTx == 0 is used to signal that all parent block's transaction data is available.
        assert(pindex->nHeight == nHeight);                                                                          // nHeight must be consistent.
        assert(pindex->pprev == NULL || pindex->nChainWork >= pindex->pprev->nChainWork);                            // For every block except the genesis block, the chainwork must be larger than the parent's.
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight < nHeight)));                                // The pskip pointer must point back for all but the first 2 blocks.
        assert(pindexFirstNotTreeValid == NULL);                                                                     // All mapBlockIndex entries must at least be TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE) assert(pindexFirstNotTreeValid == NULL);       // TREE valid implies all parents are TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN) assert(pindexFirstNotChainValid == NULL);     // CHAIN valid implies all parents are CHAIN valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS) assert(pindexFirstNotScriptsValid == NULL); // SCRIPTS valid implies all parents are SCRIPTS valid
        if (pindexFirstInvalid == NULL) {
            // Checks for not-invalid blocks.
            assert((pindex->nStatus & BLOCK_FAILED_MASK) == 0); // The failed mask cannot be set for blocks without invalid parents.
        }
        if (!CBlockIndexWorkComparator()(pindex, chain.Tip()) && pindexFirstMissing == NULL) {
            if (pindexFirstInvalid == NULL) { // If this block sorts at least as good as the current tip and is valid, it must be in setBlockIndexCandidates.
                assert(setBlockIndexCandidates.count(pindex));
            }
        } else { // If this block sorts worse than the current tip, it cannot be in setBlockIndexCandidates.
            assert(setBlockIndexCandidates.count(pindex) == 0);
        }
        // Check whether this block is in mapBlocksUnlinked.
        std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> rangeUnlinked = mapBlocksUnlinked.equal_range(pindex->pprev);
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second) {
            assert(rangeUnlinked.first->first == pindex->pprev);
            if (rangeUnlinked.first->second == pindex) {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (pindex->pprev && pindex->nStatus & BLOCK_HAVE_DATA && pindexFirstMissing != NULL) {
            if (pindexFirstInvalid == NULL) { // If this block has block data available, some parent doesn't, and has no invalid parents, it must be in mapBlocksUnlinked.
                assert(foundInUnlinked);
            }
        } else { // If this block does not have block data available, or all parents do, it cannot be in mapBlocksUnlinked.
            assert(!foundInUnlinked);
        }
        // assert(pindex->GetBlockHash() == pindex->GetBlockHeader().GetHash()); // Perhaps too slow
        // End: actual consistency checks.

        // Try descending into the first subnode.
        std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = forward.equal_range(pindex);
        if (range.first != range.second) {
            // A subnode was found.
            pindex = range.first->second;
            nHeight++;
            continue;
        }
        // This is a leaf node.
        // Move upwards until we reach a node of which we have not yet visited the last child.
        while (pindex) {
            // We are going to either move to a parent or a sibling of pindex.
            // If pindex was the first with a certain property, unset the corresponding variable.
            if (pindex == pindexFirstInvalid) pindexFirstInvalid = NULL;
            if (pindex == pindexFirstMissing) pindexFirstMissing = NULL;
            if (pindex == pindexFirstNotTreeValid) pindexFirstNotTreeValid = NULL;
            if (pindex == pindexFirstNotChainValid) pindexFirstNotChainValid = NULL;
            if (pindex == pindexFirstNotScriptsValid) pindexFirstNotScriptsValid = NULL;
            // Find our parent.
            CBlockIndex* pindexPar = pindex->pprev;
            // Find which child we just visited.
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> rangePar = forward.equal_range(pindexPar);
            while (rangePar.first->second != pindex) {
                assert(rangePar.first != rangePar.second); // Our parent must have at least the node we're coming from as child.
                rangePar.first++;
            }
            // Proceed to the next one.
            rangePar.first++;
            if (rangePar.first != rangePar.second) {
                // Move to the sibling.
                pindex = rangePar.first->second;
                break;
            } else {
                // Move up further.
                pindex = pindexPar;
                nHeight--;
                continue;
            }
        }
    }

    // Check that we actually traversed the entire map.
    assert(nNodes == forward.size());
}

//////////////////////////////////////////////////////////////////////////////
//
// CAlert
//

string GetWarnings(string strFor)
{
    int nPriority = 0;
    string strStatusBar;
    string strRPC;

    if (!CLIENT_VERSION_IS_RELEASE)
        strStatusBar = translate("This is a pre-release test build - use at your own risk - do not use for staking or merchant applications!");

    if (settings.GetBoolArg("-testsafemode", false))
        strStatusBar = strRPC = "testsafemode enabled";

    // Misc warnings like out of disk space and clock is wrong
    if (strMiscWarning != "") {
        nPriority = 1000;
        strStatusBar = strMiscWarning;
    }

    if (fLargeWorkForkFound) {
        nPriority = 2000;
        strStatusBar = strRPC = translate("Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.");
    } else if (fLargeWorkInvalidChainFound) {
        nPriority = 2000;
        strStatusBar = strRPC = translate("Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade.");
    }

    // Alerts
    CAlert::GetHighestPriorityWarning(nPriority,strStatusBar);

    if (strFor == "statusbar")
        return strStatusBar;
    else if (strFor == "rpc")
        return strRPC;
    assert(!"GetWarnings() : invalid parameter");
    return "error";
}


//////////////////////////////////////////////////////////////////////////////
//
// Messages
//


bool static AlreadyHave(const CInv& inv)
{
    switch (inv.GetType()) {
    case MSG_TX: {
        bool txInMap = false;
        txInMap = mempool.exists(inv.GetHash());
        return txInMap || OrphanTransactionIsKnown(inv.GetHash());
    }

    case MSG_BLOCK: {
        const ChainstateManager::Reference chainstate;
        return chainstate->GetBlockMap().count(inv.GetHash()) > 0;
    }
    case MSG_TXLOCK_REQUEST:
        return false;
    case MSG_TXLOCK_VOTE:
        return false;
    case MSG_SPORK:
        return SporkDataIsKnown(inv.GetHash());
    case MSG_MASTERNODE_WINNER:
        return MasternodeWinnerIsKnown(inv.GetHash());
    case MSG_MASTERNODE_ANNOUNCE:
        return MasternodeIsKnown(inv.GetHash());
    case MSG_MASTERNODE_PING:
        return MasternodePingIsKnown(inv.GetHash());
    }
    // Don't know what it is, just say we already got one
    return true;
}

static bool PushKnownInventory(CNode* pfrom, const CInv& inv)
{
    bool pushed = false;
    InventoryType type = static_cast<InventoryType>(inv.GetType());
    switch (type)
    {
    case InventoryType::MSG_TX:
        {
            CTransaction tx;
            if (mempool.lookup(inv.GetHash(), tx))
            {
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss.reserve(1000);
                ss << tx;
                pfrom->PushMessage("tx", ss);
                pushed = true;
            }
        }
        break;
    case InventoryType::MSG_SPORK:
        pushed = ShareSporkDataWithPeer(pfrom,inv.GetHash());
        break;
    case InventoryType::MSG_MASTERNODE_WINNER:
        pushed = ShareMasternodeWinnerWithPeer(pfrom,inv.GetHash());
        break;
    case InventoryType::MSG_MASTERNODE_ANNOUNCE:
        pushed = ShareMasternodeBroadcastWithPeer(pfrom,inv.GetHash());
        break;
    case InventoryType::MSG_MASTERNODE_PING:
        pushed = ShareMasternodePingWithPeer(pfrom,inv.GetHash());
        break;
    case InventoryType::MSG_FILTERED_BLOCK:
    case InventoryType::MSG_TXLOCK_REQUEST:
    case InventoryType::MSG_BUDGET_VOTE:
    case InventoryType::MSG_MASTERNODE_SCANNING_ERROR:
    case InventoryType::MSG_BUDGET_PROPOSAL:
    case InventoryType::MSG_BUDGET_FINALIZED:
    case InventoryType::MSG_BUDGET_FINALIZED_VOTE:
    case InventoryType::MSG_MASTERNODE_QUORUM:
    default:
        break;
    }
    return pushed;
}

static std::pair<const CBlockIndex*, bool> GetBlockIndexOfRequestedBlock(NodeId nodeId, const uint256& blockHash)
{
    bool send = false;
    CBlockIndex* pindex = nullptr;
    {
        LOCK(cs_main);

        const ChainstateManager::Reference chainstate;
        const auto& blockMap = chainstate->GetBlockMap();
        const auto& chain = chainstate->ActiveChain();

        const auto mi = blockMap.find(blockHash);
        if (mi != blockMap.end())
        {
            pindex = mi->second;
            if (chain.Contains(mi->second)) {
                send = true;
            } else {
                // To prevent fingerprinting attacks, only send blocks outside of the active
                // chain if they are valid, and no more than a max reorg depth than the best header
                // chain we know about.
                send = mi->second->IsValid(BLOCK_VALID_SCRIPTS) && (pindexBestHeader != NULL) &&
                        (chain.Height() - mi->second->nHeight < Params().MaxReorganizationDepth());
                if (!send) {
                    LogPrintf("%s: ignoring request from peer=%i for old block that isn't in the main chain\n",__func__, nodeId);
                }
            }
        }
    }
    return std::make_pair(pindex,send);
}

static void PushCorrespondingBlockToPeer(CNode* pfrom, const CBlockIndex* blockToPush,bool isBlock)
{
    // Send block from disk
    CBlock block;
    if (!ReadBlockFromDisk(block, blockToPush))
        assert(!"cannot load block from disk");
    if (isBlock)
    {
        pfrom->PushMessage("block", block);
    }
    else // MSG_FILTERED_BLOCK)
    {
        LOCK(pfrom->cs_filter);
        if (pfrom->pfilter) {
            CMerkleBlock merkleBlock(block, *pfrom->pfilter);
            pfrom->PushMessage("merkleblock", merkleBlock);
            // CMerkleBlock just contains hashes, so also push any transactions in the block the client did not see
            // This avoids hurting performance by pointlessly requiring a round-trip
            // Note that there is currently no way for a node to request any single transactions we didnt send here -
            // they must either disconnect and retry or request the full block.
            // Thus, the protocol spec specified allows for us to provide duplicate txn here,
            // however we MUST always provide at least what the remote peer needs
            typedef std::pair<unsigned int, uint256> PairType;
            for(PairType& pair: merkleBlock.vMatchedTxn)
            {
                if (!pfrom->setInventoryKnown.count(CInv(MSG_TX, pair.second)))
                    pfrom->PushMessage("tx", block.vtx[pair.first]);
            }
        }
        // else
        // no response
    }
}

void static ProcessGetData(CNode* pfrom, std::deque<CInv>& requestsForData)
{
    const ChainstateManager::Reference chainstate;

    auto it = requestsForData.begin();

    std::vector<CInv> vNotFound;

    while (
        it != requestsForData.end() &&
        pfrom->GetSendBufferStatus() != NodeBufferStatus::IS_FULL)
    {
        const CInv& inv = *it;
        {
            boost::this_thread::interruption_point();
            it++;

            if (inv.GetType() == MSG_BLOCK || inv.GetType() == MSG_FILTERED_BLOCK)
            {
                std::pair<const CBlockIndex*, bool> blockIndexAndSendStatus = GetBlockIndexOfRequestedBlock(pfrom->GetId(),inv.GetHash());
                // Don't send not-validated blocks
                if (blockIndexAndSendStatus.second &&
                    (blockIndexAndSendStatus.first->nStatus & BLOCK_HAVE_DATA))
                {
                    PushCorrespondingBlockToPeer(pfrom, blockIndexAndSendStatus.first,inv.GetType() == MSG_BLOCK);

                    // Trigger them to send a getblocks request for the next batch of inventory
                    if (inv.GetHash() == pfrom->hashContinue) {
                        // Bypass PushInventory, this must send even if redundant,
                        // and we want it right after the last block so they don't
                        // wait for other stuff first.
                        std::vector<CInv> vInv;
                        vInv.push_back(CInv(MSG_BLOCK, chainstate->ActiveChain().Tip()->GetBlockHash()));
                        pfrom->PushMessage("inv", vInv);
                        pfrom->hashContinue = 0;
                    }
                }
            }
            else if (inv.IsKnownType())
            {
                // Send stream from relay memory
                if(!RepeatRelayedInventory(pfrom,inv) && !PushKnownInventory(pfrom,inv))
                {
                    vNotFound.push_back(inv);
                }
            }

            if (inv.GetType() == MSG_BLOCK || inv.GetType() == MSG_FILTERED_BLOCK)
                break;
        }
    }

    requestsForData.erase(requestsForData.begin(), it);

    if (!vNotFound.empty()) {
        // Let the peer know that we didn't find what it asked for, so it doesn't
        // have to wait around forever. Currently only SPV clients actually care
        // about this message: it's needed when they are recursively walking the
        // dependencies of relevant unconfirmed transactions. SPV clients want to
        // do that because they want to know about (and store and rebroadcast and
        // risk analyze) the dependencies of transactions relevant to them, without
        // having to download the entire memory pool.
        pfrom->PushMessage("notfound", vNotFound);
    }
}

void RespondToRequestForDataFrom(CNode* pfrom)
{
    ProcessGetData(pfrom, pfrom->GetRequestForDataQueue());
}

constexpr const char* NetworkMessageType_VERSION = "version";
static bool SetPeerVersionAndServices(CNode* pfrom, CAddrMan& addrman, CDataStream& vRecv)
{
    // Each connection can only send one version message
    static const std::string lastCommand = std::string(NetworkMessageType_VERSION);
    if (pfrom->nVersion != 0) {
        pfrom->PushMessage("reject", lastCommand, REJECT_DUPLICATE, string("Duplicate version message"));
        Misbehaving(pfrom->GetNodeState(), 1,"Duplicated version message");
        return false;
    }

    int64_t nTime;
    CAddress addrMe;
    CAddress addrFrom;
    uint64_t nNonce = 1;
    vRecv >> pfrom->nVersion >> pfrom->nServices >> nTime >> addrMe;
    if (pfrom->DisconnectOldProtocol(ActiveProtocol(), lastCommand))
    {
        PeerBanningService::Ban(GetTime(),pfrom->addr);
        return false;
    }

    if (pfrom->nVersion == 10300)
        pfrom->nVersion = 300;
    if (!vRecv.empty())
        vRecv >> addrFrom >> nNonce;
    if (!vRecv.empty()) {
        vRecv >> LIMITED_STRING(pfrom->strSubVer, 256);
        pfrom->cleanSubVer = SanitizeString(pfrom->strSubVer);
    }
    if (!vRecv.empty())
        vRecv >> pfrom->nStartingHeight;
    if (!vRecv.empty())
        vRecv >> pfrom->fRelayTxes; // set to true after we get the first filter* message
    else
        pfrom->fRelayTxes = true;

    // Disconnect if we connected to ourself
    if (pfrom->IsSelfConnection(nNonce))
    {
        LogPrintf("connected to self at %s, disconnecting\n", pfrom->addr);
        pfrom->FlagForDisconnection();
        return true;
    }

    pfrom->addrLocal = addrMe;
    if (pfrom->fInbound && addrMe.IsRoutable()) {
        SeenLocal(addrMe);
    }

    // Be shy and don't send version until we hear
    if (pfrom->fInbound)
        pfrom->PushVersion(GetHeight());

    pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);

    // Potentially mark this peer as a preferred download peer.
    pfrom->UpdatePreferredDownloadStatus();

    // Change version
    pfrom->PushMessage("verack");

    if(pfrom->fInbound) {
        pfrom->PushMessage("sporkcount", GetSporkManager().GetActiveSporkCount());
    }

    pfrom->SetOutboundSerializationVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

    if (!pfrom->fInbound) {
        // Advertise our address
        if (IsListening() && !IsInitialBlockDownload()) {
            CAddress addr = GetLocalAddress(&pfrom->addr);
            if (addr.IsRoutable()) {
                LogPrintf("%s: advertizing address %s\n",__func__, addr);
                pfrom->PushAddress(addr);
            } else if (PeersLocalAddressIsGood(pfrom)) {
                addr.SetIP(pfrom->addrLocal);
                LogPrintf("%s: advertizing address %s\n",__func__, addr);
                pfrom->PushAddress(addr);
            }
        }

        // Get recent addresses
        if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || addrman.size() < 1000) {
            pfrom->PushMessage("getaddr");
            pfrom->fGetAddr = true;
        }
        addrman.Good(pfrom->addr);
    } else {
        if (((CNetAddr)pfrom->addr) == (CNetAddr)addrFrom) {
            addrman.Add(addrFrom, addrFrom);
            addrman.Good(addrFrom);
        }
    }

    // Relay alerts
    RelayAllAlertsTo(pfrom);

    pfrom->fSuccessfullyConnected = true;

    string remoteAddr;
    if (ShouldLogPeerIPs())
        remoteAddr = ", peeraddr=" + pfrom->addr.ToString();

    LogPrintf("receive version message: %s: version %d, blocks=%d, us=%s, peer=%d%s\n",
                pfrom->cleanSubVer, pfrom->nVersion,
                pfrom->nStartingHeight, addrMe, pfrom->id,
                remoteAddr);

    AddTimeData(pfrom->addr, nTime);
    return true;
}

bool static ProcessMessage(CNode* pfrom, std::string strCommand, CDataStream& vRecv, int64_t nTimeReceived)
{
    static CAddrMan& addrman = GetNetworkAddressManager();
    LogPrint("net","received: %s (%u bytes) peer=%d\n", SanitizeString(strCommand), vRecv.size(), pfrom->id);
    if (settings.ParameterIsSet("-dropmessagestest") && GetRand(atoi(settings.GetParameter("-dropmessagestest"))) == 0) {
        LogPrint("net","dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    const ChainstateManager::Reference chainstate;
    const auto& coinsTip = chainstate->CoinsTip();
    const auto& blockMap = chainstate->GetBlockMap();
    const auto& chain = chainstate->ActiveChain();

    if (strCommand == std::string(NetworkMessageType_VERSION))
    {
        if(!SetPeerVersionAndServices(pfrom,addrman,vRecv))
        {
            return false;
        }
        return true;
    }
    else if (pfrom->nVersion == 0)
    {
        // Must have a version message before anything else
        Misbehaving(pfrom->GetNodeState(), 1,"Version message has not arrived before other handshake steps");
        return false;
    }
    else if (strCommand == "verack")
    {
        pfrom->SetInboundSerializationVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        // Mark this node as currently connected, so we update its timestamp later.
        if (pfrom->fNetworkNode) {
            LOCK(cs_main);
            pfrom->SetToCurrentlyConnected();
        }
        return true;
    }

    if (strCommand == "ping")
    {
        pfrom->ProcessReceivedPing(vRecv);
        return true;
    }
    else if (strCommand == "pong")
    {
        pfrom->ProcessReceivedPong(vRecv,nTimeReceived);
        return true;
    }

    if (strCommand == "addr")
    {
        std::vector<CAddress> vAddr;
        vRecv >> vAddr;

        // Don't want addr from older versions unless seeding
        if (pfrom->nVersion < CADDR_TIME_VERSION && addrman.size() > 1000)
            return true;
        if (vAddr.size() > 1000) {
            Misbehaving(pfrom->GetNodeState(), 20,"Requested too many addresses");
            return error("message addr size() = %u", vAddr.size());
        }

        // Store the new addresses
        std::vector<CAddress> vAddrOk;
        int64_t nNow = GetAdjustedTime();
        int64_t nSince = nNow - 10 * 60;
        for(CAddress& addr: vAddr) {
            boost::this_thread::interruption_point();

            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable()) {
                // Relay to a limited number of other nodes
                DeterministicallyRelayAddressToLimitedPeers(addr,fReachable ? 2 : 1);
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
        }
        addrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);
        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        if (pfrom->fOneShot)
            pfrom->FlagForDisconnection();
    }
    else if (strCommand == "inv")
    {
        std::vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ) {
            Misbehaving(pfrom->GetNodeState(), 20, "Asked for too large an inventory of data items");
            return error("message inv size() = %u", vInv.size());
        }

        std::vector<CInv> vToFetch;
        std::vector<const CInv*> blockInventory;
        blockInventory.reserve(vInv.size());
        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++) {
            const CInv& inv = vInv[nInv];

            boost::this_thread::interruption_point();
            pfrom->AddInventoryKnown(inv);

            bool fAlreadyHave = AlreadyHave(inv);
            LogPrint("net", "got inv: %s  %s peer=%d\n", inv, fAlreadyHave ? "have" : "new", pfrom->id);

            if (!fAlreadyHave && !fImporting && !fReindex && inv.GetType() != MSG_BLOCK)
                pfrom->AskFor(inv);


            if (inv.GetType() == MSG_BLOCK) {
                UpdateBlockAvailability(blockMap, pfrom->GetNodeState(), inv.GetHash());
                if (!fAlreadyHave && !fImporting && !fReindex) {
                    // Add this to the list of blocks to request
                    blockInventory.push_back(&inv);
                }
            }

            if (pfrom->GetSendBufferStatus()==NodeBufferStatus::IS_OVERFLOWED) {
                Misbehaving(pfrom->GetNodeState(), 50,"Overflowed message buffer");
                return error("Peer %d has exceeded send buffer size", pfrom->GetId());
            }
        }
        {
            LOCK(cs_main);
            for(const CInv* blockInventoryReference: blockInventory)
            {
                if(!BlockIsInFlight(blockInventoryReference->GetHash()))
                {
                    vToFetch.push_back(*blockInventoryReference);
                    LogPrint("net", "getblocks (%d) %s to peer=%d\n", pindexBestHeader->nHeight, blockInventoryReference->GetHash(), pfrom->id);
                }
            }
        }

        if (!vToFetch.empty())
            pfrom->PushMessage("getdata", vToFetch);
    }
    else if (strCommand == "getdata")
    {
        std::vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ) {
            Misbehaving(pfrom->GetNodeState(), 20, "Getdata request too large");
            return error("message getdata size() = %u", vInv.size());
        }

        pfrom->RecordRequestForData(vInv);
        pfrom->RespondToRequestForData();
    }
    else if (strCommand == "getblocks" || strCommand == "getheaders")
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        /* We build up the inventory while holding cs_main (since we access
           a lot of global state, especially chainActive), but then send it
           to the peer without holding onto the lock anymore.  */
        std::vector<CInv> vInv;

        {
        LOCK(cs_main);

        // Find the last block the caller has in the main chain
        const CBlockIndex* pindex = FindForkInGlobalIndex(chain, locator);

        // Send the rest of the chain
        if (pindex)
            pindex = chain.Next(pindex);
        int nLimit = 500;
        LogPrint("net", "getblocks %d to %s limit %d from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop == uint256(0) ? "end" : hashStop.ToString(), nLimit, pfrom->id);
        for (; pindex; pindex = chain.Next(pindex)) {
            // Make sure the inv messages for the requested chain are sent
            // in any case, even if e.g. we have already announced those
            // blocks in the past.  This ensures that the peer will be able
            // to sync properly and not get stuck.
            vInv.emplace_back(MSG_BLOCK, pindex->GetBlockHash());
            if (--nLimit <= 0) {
                // When this block is requested, we'll send an inv that'll make them
                // getblocks the next batch of inventory.
                LogPrint("net", "  getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash());
                pfrom->hashContinue = pindex->GetBlockHash();
                break;
            }
            if (pindex->GetBlockHash() == hashStop) {
                LogPrint("net", "  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash());
                break;
            }
        }
        }

        if (!vInv.empty())
            pfrom->PushMessage("inv", vInv);
    }
    else if (strCommand == "headers" && Params().HeadersFirstSyncingActive())
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        if (IsInitialBlockDownload())
            return true;

        const CBlockIndex* pindex = nullptr;
        if (locator.IsNull()) {
            // If locator is null, return the hashStop block
            const auto mi = blockMap.find(hashStop);
            if (mi == blockMap.end())
                return true;
            pindex = (*mi).second;
        } else {
            // Find the last block the caller has in the main chain
            pindex = FindForkInGlobalIndex(chain, locator);
            if (pindex)
                pindex = chain.Next(pindex);
        }

        // we must use CBlocks, as CBlockHeaders won't include the 0x00 nTx count at the end
        std::vector<CBlock> vHeaders;
        int nLimit = MAX_HEADERS_RESULTS;
        LogPrint("net","getheaders %d to %s from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop, pfrom->id);
        for (; pindex; pindex = chain.Next(pindex)) {
            vHeaders.push_back(pindex->GetBlockHeader());
            if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                break;
        }
        pfrom->PushMessage("headers", vHeaders);
    }
    else if (strCommand == "tx" || strCommand == "dstx")
    {
        std::vector<uint256> vWorkQueue;
        std::vector<uint256> vEraseQueue;
        CTransaction tx;

        //masternode signed transaction
        CTxIn vin;
        std::vector<unsigned char> vchSig;

        if (strCommand == "tx") {
            vRecv >> tx;
        }

        CInv inv(MSG_TX, tx.GetHash());
        pfrom->AddInventoryKnown(inv);

        bool fMissingInputs = false;
        CValidationState state;

        {
        LOCK(cs_main);

        CNode::ClearInventoryItem(inv);

        if ( AcceptToMemoryPool(mempool, state, tx, true, &fMissingInputs))
        {
            mempool.check(&coinsTip, blockMap);
            RelayTransactionToAllPeers(tx);
            vWorkQueue.push_back(inv.GetHash());

            LogPrint("mempool", "%s: peer=%d %s : accepted %s (poolsz %u)\n",
                    __func__,
                     pfrom->id, pfrom->cleanSubVer,
                     tx.ToStringShort(),
                     mempool.mapTx.size());

            // Recursively process any orphan transactions that depended on this one
            std::set<NodeId> setMisbehaving;
            for(unsigned int i = 0; i < vWorkQueue.size(); i++)
            {
                const std::set<uint256>& spendingTransactionIds = GetOrphanSpendingTransactionIds(vWorkQueue[i]);
                for(const uint256 &orphanHash: spendingTransactionIds)
                {
                    NodeId fromPeer;
                    const CTransaction &orphanTx = GetOrphanTransaction(orphanHash,fromPeer);
                    bool fMissingInputs2 = false;
                    // Use a dummy CValidationState so someone can't setup nodes to counter-DoS based on orphan
                    // resolution (that is, feeding people an invalid transaction based on LegitTxX in order to get
                    // anyone relaying LegitTxX banned)
                    CValidationState stateDummy;


                    if(setMisbehaving.count(fromPeer))
                        continue;
                    if(AcceptToMemoryPool(mempool, stateDummy, orphanTx, true, &fMissingInputs2)) {
                        LogPrint("mempool", "   accepted orphan tx %s\n", orphanHash);
                        RelayTransactionToAllPeers(orphanTx);
                        vWorkQueue.push_back(orphanHash);
                        vEraseQueue.push_back(orphanHash);
                    } else if(!fMissingInputs2) {
                        int nDos = 0;
                        if(stateDummy.IsInvalid(nDos) && nDos > 0) {
                            // Punish peer that gave us an invalid orphan tx
                            Misbehaving(fromPeer, nDos, "Invalid orphan transaction required by mempool transaction");
                            setMisbehaving.insert(fromPeer);
                            LogPrint("mempool", "   invalid orphan tx %s\n", orphanHash);
                        }
                        // Has inputs but not accepted to mempool
                        // Probably non-standard or insufficient fee/priority
                        LogPrint("mempool", "   removed orphan tx %s\n", orphanHash);
                        vEraseQueue.push_back(orphanHash);
                    }
                    mempool.check(&coinsTip, blockMap);
                }
            }

            for(uint256 hash: vEraseQueue)EraseOrphanTx(hash);
        }
        else if (fMissingInputs)
        {
            AddOrphanTx(tx, pfrom->GetId());

            // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
            unsigned int nMaxOrphanTx = (unsigned int)std::max((int64_t)0, settings.GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS));
            unsigned int nEvicted = LimitOrphanTxSize(nMaxOrphanTx);
            if (nEvicted > 0)
                LogPrint("mempool", "mapOrphan overflow, removed %u tx\n", nEvicted);
        } else if (pfrom->fWhitelisted) {
            // Always relay transactions received from whitelisted peers, even
            // if they are already in the mempool (allowing the node to function
            // as a gateway for nodes hidden behind it).

            RelayTransactionToAllPeers(tx);
        }
        } // cs_main

        int nDoS = 0;
        if (state.IsInvalid(nDoS)) {
            LogPrint("mempool", "%s from peer=%d %s was not accepted into the memory pool: %s\n", tx.ToStringShort(),
                     pfrom->id, pfrom->cleanSubVer,
                     state.GetRejectReason());
            pfrom->PushMessage("reject", strCommand, state.GetRejectCode(),
                               state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.GetHash());
            if (nDoS > 0)
                Misbehaving(pfrom->GetNodeState(), nDoS, "Transaction from peer rejected by memory pool");
        }
    }
    else if (strCommand == "headers" && Params().HeadersFirstSyncingActive() && !fImporting && !fReindex) // Ignore headers received while importing
    {
        std::vector<CBlockHeader> headers;

        // Bypass the normal CBlock deserialization, as we don't want to risk deserializing 2000 full blocks.
        unsigned int nCount = ReadCompactSize(vRecv);
        if (nCount > MAX_HEADERS_RESULTS) {
            Misbehaving(pfrom->GetNodeState(), 20, "Maximum number of headers exceeded");
            return error("headers message size = %u", nCount);
        }
        headers.resize(nCount);
        for (unsigned int n = 0; n < nCount; n++) {
            vRecv >> headers[n];
            ReadCompactSize(vRecv); // ignore tx count; assume it is 0.
        }

        LOCK(cs_main);

        if (nCount == 0) {
            // Nothing interesting. Stop asking this peers for more headers.
            return true;
        }
        CBlockIndex* pindexLast = NULL;
        for(const CBlockHeader& header: headers) {
            CValidationState state;
            if (pindexLast != NULL && header.hashPrevBlock != pindexLast->GetBlockHash()) {
                Misbehaving(pfrom->GetNodeState(), 20, "Non-contiguous headers submitted by peer");
                return error("non-continuous headers sequence");
            }

            /*TODO: this has a CBlock cast on it so that it will compile. There should be a solution for this
             * before headers are reimplemented on mainnet
             */
            if (!AcceptBlockHeader((CBlock)header, state, &pindexLast)) {
                int nDoS;
                if (state.IsInvalid(nDoS)) {
                    if (nDoS > 0)
                        Misbehaving(pfrom->GetNodeState(), nDoS, "Invalid block header received");
                    std::string strError = "invalid header received " + header.GetHash().ToString();
                    return error(strError.c_str());
                }
            }
        }

        if (pindexLast)
            UpdateBlockAvailability(blockMap, pfrom->GetNodeState(), pindexLast->GetBlockHash());

        if (nCount == MAX_HEADERS_RESULTS && pindexLast) {
            // Headers message had its maximum size; the peer may have more headers.
            // TODO: optimize: if pindexLast is an ancestor of chainActive.Tip or pindexBestHeader, continue
            // from there instead.
            LogPrintf("more getheaders (%d) to end to peer=%d (startheight:%d)\n", pindexLast->nHeight, pfrom->id, pfrom->nStartingHeight);
            pfrom->PushMessage("getheaders", chain.GetLocator(pindexLast), uint256(0));
        }

        CheckBlockIndex();
    }
    else if (strCommand == "block" && !fImporting && !fReindex) // Ignore blocks received while importing
    {
        CBlock block;
        vRecv >> block;
        uint256 hashBlock = block.GetHash();
        CInv inv(MSG_BLOCK, hashBlock);
        LogPrint("net", "received block %s peer=%d\n", inv.GetHash(), pfrom->id);

        //sometimes we will be sent their most recent block and its not the one we want, in that case tell where we are
        if (!blockMap.count(block.hashPrevBlock)) {
            if (find(pfrom->vBlockRequested.begin(), pfrom->vBlockRequested.end(), hashBlock) != pfrom->vBlockRequested.end()) {
                //we already asked for this block, so lets work backwards and ask for the previous block
                pfrom->PushMessage("getblocks", chain.GetLocator(), block.hashPrevBlock);
                pfrom->vBlockRequested.push_back(block.hashPrevBlock);
            } else {
                //ask to sync to this block
                pfrom->PushMessage("getblocks", chain.GetLocator(), hashBlock);
                pfrom->vBlockRequested.push_back(hashBlock);
            }
        } else {
            pfrom->AddInventoryKnown(inv);

            CValidationState state;
            if (!blockMap.count(block.GetHash())) {
                ProcessNewBlock(state, pfrom, &block);
                int nDoS;
                if(state.IsInvalid(nDoS)) {
                    pfrom->PushMessage("reject", strCommand, state.GetRejectCode(),
                                       state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.GetHash());
                    if(nDoS > 0) {
                        TRY_LOCK(cs_main, lockMain);
                        if(lockMain) Misbehaving(pfrom->GetNodeState(), nDoS, "Bad block processed");
                    }
                }
                //disconnect this node if its old protocol version
                if(pfrom->DisconnectOldProtocol(ActiveProtocol(), strCommand))
                {
                    PeerBanningService::Ban(GetTime(),pfrom->addr);
                }
            } else {
                LogPrint("net", "%s : Already processed block %s, skipping ProcessNewBlock()\n", __func__, block.GetHash());
            }
        }
    }
    // This asymmetric behavior for inbound and outbound connections was introduced
    // to prevent a fingerprinting attack: an attacker can send specific fake addresses
    // to users' AddrMan and later request them by sending getaddr messages.
    // Making users (which are behind NAT and can only make outgoing connections) ignore
    // getaddr message mitigates the attack.
    else if ((strCommand == "getaddr") && (pfrom->fInbound))
    {
        pfrom->vAddrToSend.clear();
        std::vector<CAddress> vAddr = addrman.GetAddr();
        for(const CAddress& addr: vAddr)
                pfrom->PushAddress(addr);
    }
    else if (strCommand == "alert" && AlertsAreEnabled())
    {
        CAlert alert;
        vRecv >> alert;

        uint256 alertHash = alert.GetHash();
        if (pfrom->setKnown.count(alertHash) == 0) {
            if (alert.ProcessAlert(settings)) {
                // Relay
                pfrom->setKnown.insert(alertHash);
                RelayAlertToPeers(alert);
            } else {
                // Small DoS penalty so peers that send us lots of
                // duplicate/expired/invalid-signature/whatever alerts
                // eventually get banned.
                // This isn't a Misbehaving(100) (immediate ban) because the
                // peer might be an older or different implementation with
                // a different signature key, etc.
                Misbehaving(pfrom->GetNodeState(), 10, "Unable to process alert message");
            }
        }
    }
    else if (!BloomFiltersAreEnabled() &&
             (strCommand == "filterload" ||
              strCommand == "filteradd" ||
              strCommand == "filterclear"))
    {
        LogPrintf("bloom message=%s\n", strCommand);
        Misbehaving(pfrom->GetNodeState(), 100, "Sent bloom filter msg but they are disabled");
    }
    else if (strCommand == "filterload")
    {
        CBloomFilter filter;
        vRecv >> filter;

        if (!filter.IsWithinSizeConstraints())
            // There is no excuse for sending a too-large filter
            Misbehaving(pfrom->GetNodeState(), 100,"Sent too large a bloom filter message");
        else {
            LOCK(pfrom->cs_filter);
            delete pfrom->pfilter;
            pfrom->pfilter = new CBloomFilter(filter);
            pfrom->pfilter->UpdateEmptyFull();
        }
        pfrom->fRelayTxes = true;
    }
    else if (strCommand == "filteradd")
    {
        std::vector<unsigned char> vData;
        vRecv >> vData;

        // Nodes must NEVER send a data item > 520 bytes (the max size for a script data object,
        // and thus, the maximum size any matched object can have) in a filteradd message
        if (vData.size() > MAX_SCRIPT_ELEMENT_SIZE) {
            Misbehaving(pfrom->GetNodeState(), 100, "Exceeded maximum size of script element to be added to filter");
        } else {
            LOCK(pfrom->cs_filter);
            if (pfrom->pfilter)
                pfrom->pfilter->insert(vData);
            else
                Misbehaving(pfrom->GetNodeState(), 100, "Attempted to load filter before enabling it");
        }
    }
    else if (strCommand == "filterclear")
    {
        LOCK(pfrom->cs_filter);
        delete pfrom->pfilter;
        pfrom->pfilter = new CBloomFilter();
        pfrom->fRelayTxes = true;
    }
    else if (strCommand == "reject")
    {
        if (fDebug) {
            try {
                string strMsg;
                unsigned char ccode;
                string strReason;
                vRecv >> LIMITED_STRING(strMsg, CMessageHeader::COMMAND_SIZE) >> ccode >> LIMITED_STRING(strReason, MAX_REJECT_MESSAGE_LENGTH);

                ostringstream ss;
                ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

                if (strMsg == "block" || strMsg == "tx") {
                    uint256 hash;
                    vRecv >> hash;
                    ss << ": hash " << hash.ToString();
                }
                LogPrint("net", "Reject %s\n", SanitizeString(ss.str()));
            } catch (std::ios_base::failure& e) {
                // Avoid feedback loops by preventing reject messages from triggering a new reject message.
                LogPrint("net", "Unparseable reject message received\n");
            }
        }
    } else {
        ProcessSpork(pfrom, strCommand, vRecv);
        ProcessMasternodeMessages(pfrom,strCommand,vRecv);
    }

    return true;
}

enum NetworkMessageState
{
    SKIP_MESSAGE,
    STOP_PROCESSING,
    VALID,
};
static NetworkMessageState CheckNetworkMessageHeader(
    NodeId id,
    const CNetMessage& msg,
    bool& fOk)
{
    // Scan for message start
    if (memcmp(msg.hdr.pchMessageStart, Params().MessageStart(), MESSAGE_START_SIZE) != 0) {
        LogPrintf("%s: INVALID MESSAGESTART %s peer=%d\n",__func__, SanitizeString(msg.hdr.GetCommand()), id);
        fOk = false;
        return NetworkMessageState::STOP_PROCESSING;
    }

    // Read header
    const CMessageHeader& hdr = msg.hdr;
    if (!hdr.IsValid()) {
        LogPrintf("%s: ERRORS IN HEADER %s peer=%d\n",__func__, SanitizeString(hdr.GetCommand()), id);
        return NetworkMessageState::SKIP_MESSAGE;
    }

    // Message size
    const unsigned int nMessageSize = hdr.nMessageSize;

    // Checksum
    const CDataStream& vRecv = msg.vRecv;
    uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
    unsigned int nChecksum = 0;
    memcpy(&nChecksum, &hash, sizeof(nChecksum));
    if (nChecksum != hdr.nChecksum) {
        LogPrintf("%s(%s, %u bytes): CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n",
                    __func__,SanitizeString(hdr.GetCommand()), nMessageSize, nChecksum, hdr.nChecksum);
        return NetworkMessageState::SKIP_MESSAGE;
    }
    return NetworkMessageState::VALID;
}

// requires LOCK(cs_vRecvMsg)
bool ProcessReceivedMessages(CNode* pfrom)
{
    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //
    bool fOk = true;

    // this maintains the order of responses
    if(!pfrom->RespondToRequestForData()) return fOk;

    std::deque<CNetMessage>& receivedMessageQueue = pfrom->GetReceivedMessageQueue();
    std::deque<CNetMessage>::iterator iteratorToCurrentMessageToProcess = receivedMessageQueue.begin();
    std::deque<CNetMessage>::iterator iteratorToNextMessageToProcess = receivedMessageQueue.begin();
    while(
        !pfrom->IsFlaggedForDisconnection() &&
        pfrom->GetSendBufferStatus()!=NodeBufferStatus::IS_FULL && // needed, to allow responding
        iteratorToCurrentMessageToProcess != receivedMessageQueue.end() &&
        iteratorToCurrentMessageToProcess->complete()) // end, if an incomplete message is found
    {
        // get next message
        CNetMessage& msg = *iteratorToCurrentMessageToProcess;
        iteratorToNextMessageToProcess = ++iteratorToCurrentMessageToProcess;

        NetworkMessageState messageStatus = CheckNetworkMessageHeader(pfrom->GetId(), msg, fOk);
        if(messageStatus == NetworkMessageState::STOP_PROCESSING)
        {
            break;
        }
        else if(messageStatus == NetworkMessageState::SKIP_MESSAGE)
        {
            continue;
        }
        const CMessageHeader& hdr = msg.hdr;
        std::string strCommand = msg.hdr.GetCommand();

        // Process message
        bool fRet = false;
        try {
            fRet = ProcessMessage(pfrom, strCommand, msg.vRecv, msg.nTime);
            boost::this_thread::interruption_point();
        } catch (std::ios_base::failure& e) {
            pfrom->PushMessage("reject", strCommand, REJECT_MALFORMED, string("error parsing message"));
            if (strstr(e.what(), "end of data")) {
                // Allow exceptions from under-length message on vRecv
                LogPrintf("%s(%s, %u bytes): Exception '%s' caught, normally caused by a message being shorter than its stated length\n",__func__, SanitizeString(strCommand), hdr.nMessageSize, e.what());
            } else if (strstr(e.what(), "size too large")) {
                // Allow exceptions from over-long size
                LogPrintf("%s(%s, %u bytes): Exception '%s' caught\n",__func__, SanitizeString(strCommand), hdr.nMessageSize, e.what());
            } else {
                PrintExceptionContinue(&e, __func__);
            }
        } catch (boost::thread_interrupted) {
            throw;
        } catch (std::exception& e) {
            PrintExceptionContinue(&e, __func__);
        } catch (...) {
            PrintExceptionContinue(NULL, __func__);
        }

        if (!fRet)
            LogPrintf("%s(%s, %u bytes) FAILED peer=%d\n",__func__, SanitizeString(strCommand), hdr.nMessageSize, pfrom->id);

        break;
    }

    // In case the connection got shut down, its receive buffer was wiped
    if (!pfrom->IsFlaggedForDisconnection())
        receivedMessageQueue.erase(receivedMessageQueue.begin(), iteratorToNextMessageToProcess);
    else
        receivedMessageQueue.clear();

    return fOk;
}

static void SendAddresses(CNode* pto)
{
    std::vector<CAddress> vAddr;
    vAddr.reserve(pto->vAddrToSend.size());
    for(const CAddress& addr: pto->vAddrToSend) {
        // returns true if wasn't already contained in the set
        if (pto->setAddrKnown.insert(addr).second) {
            vAddr.push_back(addr);
            // receiver rejects addr messages larger than 1000
            if (vAddr.size() >= 1000) {
                pto->PushMessage("addr", vAddr);
                vAddr.clear();
            }
        }
    }
    pto->vAddrToSend.clear();
    if (!vAddr.empty())
        pto->PushMessage("addr", vAddr);
}

static void CheckForBanAndDisconnectIfNotWhitelisted(CNode* pto)
{
    CNodeState* nodeState = pto->GetNodeState();
    if(!nodeState->fShouldBan) return;
    if (pto->fWhitelisted)
    {
        LogPrintf("Warning: not punishing whitelisted peer %s!\n", pto->addr);
    }
    else
    {
        pto->FlagForDisconnection();
        if (pto->addr.IsLocal())
        {
            LogPrintf("Warning: not banning local peer %s!\n", pto->addr);
        }
        else
        {
            PeerBanningService::Ban(GetTime(),pto->addr);
        }
    }
    nodeState->fShouldBan = false;
}
static void CommunicateRejectedBlocksToPeer(CNode* pto)
{
    LOCK(cs_RejectedBlocks);
    std::vector<CBlockReject>& rejectedBlocks = rejectedBlocksByNodeId[pto->GetId()];
    for(const CBlockReject& reject: rejectedBlocks)
    {
        pto->PushMessage("reject", (std::string) "block", reject.chRejectCode, reject.strRejectReason, reject.hashBlock);
    }
    rejectedBlocks.clear();
}
static void BeginSyncingWithPeer(CNode* pto)
{
    CNodeState* state = pto->GetNodeState();
    if (!state->Syncing() && !pto->fClient && !fReindex) {
        const ChainstateManager::Reference chainstate;
        const auto& chain = chainstate->ActiveChain();

        // Only actively request headers from a single peer, unless we're close to end of initial download.
        if ( !CNodeState::NodeSyncStarted() || pindexBestHeader->GetBlockTime() > GetAdjustedTime() - 6 * 60 * 60) { // NOTE: was "close to today" and 24h in Bitcoin
            state->RecordNodeStartedToSync();
            pto->PushMessage("getblocks", chain.GetLocator(chain.Tip()), uint256(0));
        }
    }
}
static void SendInventoryToPeer(CNode* pto, bool fSendTrickle)
{
    std::vector<CInv> vInv;
    {
        std::vector<CInv> vInvWait;

        LOCK(pto->cs_inventory);
        vInv.reserve(pto->vInventoryToSend.size());
        vInvWait.reserve(pto->vInventoryToSend.size());
        for (const auto& inv : pto->vInventoryToSend) {
            if (pto->setInventoryKnown.count(inv) > 0)
                continue;

            // trickle out tx inv to protect privacy
            if (inv.GetType() == MSG_TX && !fSendTrickle) {
                // 1/4 of tx invs blast to all immediately
                static uint256 hashSalt;
                if (hashSalt == 0)
                    hashSalt = GetRandHash();
                uint256 hashRand = inv.GetHash() ^ hashSalt;
                hashRand = Hash(BEGIN(hashRand), END(hashRand));
                bool fTrickleWait = ((hashRand & 3) != 0);

                if (fTrickleWait) {
                    vInvWait.push_back(inv);
                    continue;
                }
            }

            // returns true if wasn't already contained in the set
            if (pto->setInventoryKnown.insert(inv).second) {
                vInv.push_back(inv);
                if (vInv.size() >= 1000) {
                    pto->PushMessage("inv", vInv);
                    vInv.clear();
                }
            }
        }
        pto->vInventoryToSend = std::move(vInvWait);
    }
    if (!vInv.empty())
        pto->PushMessage("inv", vInv);
}
static void RequestDisconnectionFromNodeIfStalling(int64_t nNow, CNode* pto)
{
    if (!pto->IsFlaggedForDisconnection() && BlockDownloadHasStalled(pto->GetId(),nNow, 1000000 * BLOCK_STALLING_TIMEOUT)  ) {
        // Stalling only triggers when the block download window cannot move. During normal steady state,
        // the download window should be much larger than the to-be-downloaded set of blocks, so disconnection
        // should only happen during initial block download.
        LogPrintf("Peer=%d is stalling block download, disconnecting\n", pto->id);
        pto->FlagForDisconnection();
    }
    // In case there is a block that has been in flight from this peer for (2 + 0.5 * N) times the block interval
    // (with N the number of validated blocks that were in flight at the time it was requested), disconnect due to
    // timeout. We compensate for in-flight blocks to prevent killing off peers due to our own downstream link
    // being saturated. We only count validated in-flight blocks so peers can't advertize nonexisting block hashes
    // to unreasonably increase our timeout.
    if (!pto->IsFlaggedForDisconnection() && BlockDownloadHasTimedOut(pto->GetId(),nNow,Params().TargetSpacing()) ) {
        pto->FlagForDisconnection();
    }
}
static void CollectBlockDataToRequest(int64_t nNow, CNode* pto, std::vector<CInv>& vGetData)
{
    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();
    const auto& blockMap = chainstate->GetBlockMap();

    if (!pto->IsFlaggedForDisconnection() && !pto->fClient && GetNumberOfBlocksInFlight(pto->GetId()) < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
        std::vector<const CBlockIndex*> vToDownload;
        NodeId staller = -1;
        FindNextBlocksToDownload(blockMap, chain, pto->GetNodeState(), MAX_BLOCKS_IN_TRANSIT_PER_PEER - GetNumberOfBlocksInFlight(pto->GetId()), vToDownload, staller);
        for(const auto* pindex: vToDownload) {
            vGetData.push_back(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            MarkBlockAsInFlight(pto->GetId(), pindex->GetBlockHash(), pindex);
            LogPrintf("Requesting block %s (%d) peer=%d\n", pindex->GetBlockHash(),
                        pindex->nHeight, pto->id);
        }
        if (GetNumberOfBlocksInFlight(pto->GetId()) == 0 && staller != -1) {
            RecordWhenStallingBegan(staller,nNow);
        }
    }
}
void CollectNonBlockDataToRequestAndRequestIt(CNode* pto, int64_t nNow, std::vector<CInv>& vGetData)
{
    while (!pto->IsFlaggedForDisconnection() && !pto->mapAskFor.empty() && (*pto->mapAskFor.begin()).first <= nNow)
    {
        const CInv& inv = (*pto->mapAskFor.begin()).second;
        if (!AlreadyHave(inv)) {
            LogPrint("net", "Requesting %s peer=%d\n", inv, pto->id);
            vGetData.push_back(inv);
            if (vGetData.size() >= 1000) {
                pto->PushMessage("getdata", vGetData);
                vGetData.clear();
            }
        }
        pto->mapAskFor.erase(pto->mapAskFor.begin());
    }
    if (!vGetData.empty())
        pto->PushMessage("getdata", vGetData);
}

void RebroadcastSomeMempoolTxs()
{
    TRY_LOCK(cs_main,mainLockAcquired);
    constexpr int maxNumberOfRebroadcastableTransactions = 32;
    if(mainLockAcquired)
    {
        TRY_LOCK(mempool.cs,mempoolLockAcquired);
        if(mempoolLockAcquired)
        {
            LogPrintf("Rebroadcasting mempool transactions\n");
            int numberOfTransactionsCollected = 0;
            const  std::map<uint256, CTxMemPoolEntry>& mempoolTxs = mempool.mapTx;
            for(const auto& mempoolEntryByHash: mempoolTxs)
            {
                const CTransaction& tx = mempoolEntryByHash.second.GetTx();
                bool spendsOtherMempoolTransaction = false;
                for(const auto& input: tx.vin)
                {
                    if(mempoolTxs.count(input.prevout.hash)>0)
                    {
                        spendsOtherMempoolTransaction = true;
                        break;
                    }
                }
                if(!spendsOtherMempoolTransaction)
                {
                    RelayTransactionToAllPeers(tx);
                    ++numberOfTransactionsCollected;
                }
                if(numberOfTransactionsCollected >= maxNumberOfRebroadcastableTransactions) break;
            }
        }
    }
}

void PeriodicallyRebroadcastMempoolTxs()
{
    static int64_t timeOfLastBroadcast = 0;
    static int64_t timeOfNextBroadcast = 0;
    if(GetTime() < timeOfNextBroadcast) return;
    bool nextTimeBroadcastNeedsToBeInitialized = (timeOfNextBroadcast == 0);
    timeOfNextBroadcast = GetTime() + GetRand(30*60);
    if(nextTimeBroadcastNeedsToBeInitialized) return;
    if(timeOfLastChainTipUpdate < timeOfLastBroadcast) return;
    timeOfLastBroadcast = GetTime();
    if(timeOfLastChainTipUpdate > 0)
        RebroadcastSomeMempoolTxs();
}

bool SendMessages(CNode* pto, bool fSendTrickle)
{
    const ChainstateManager::Reference chainstate;

    {
        if (fSendTrickle) {
            SendAddresses(pto);
        }

        CheckForBanAndDisconnectIfNotWhitelisted(pto);
        CommunicateRejectedBlocksToPeer(pto);

        if (pindexBestHeader == nullptr)
            pindexBestHeader = chainstate->ActiveChain().Tip();

        // Start block sync
        const CNodeState* state = pto->GetNodeState();
        bool fFetch = state->fPreferredDownload || (!CNodeState::HavePreferredDownloadPeers() && !pto->fClient && !pto->fOneShot); // Download if this is a nice peer, or we have no nice peers and this one might do.
        if(fFetch)
        {
            BeginSyncingWithPeer(pto);
        }
        if(!fReindex) PeriodicallyRebroadcastMempoolTxs();
        SendInventoryToPeer(pto,fSendTrickle);
        int64_t nNow = GetTimeMicros();
        std::vector<CInv> vGetData;
        {
            LOCK(cs_main);
            RequestDisconnectionFromNodeIfStalling(nNow,pto);
            if(fFetch) CollectBlockDataToRequest(nNow,pto,vGetData);
        }
        CollectNonBlockDataToRequestAndRequestIt(pto,nNow,vGetData);
    }
    return true;
}
