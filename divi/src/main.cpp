// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"

#include <BlockConnectionService.h>
#include <AcceptBlockValidator.h>
#include "addrman.h"
#include "alert.h"
#include <I_BlockValidator.h>
#include <BlockCheckingHelpers.h>
#include <blockmap.h>
#include "BlockFileOpener.h"
#include "BlockDiskAccessor.h"
#include <BlockRejects.h>
#include "BlockRewards.h"
#include "BlockSigning.h"
#include <ChainstateManager.h>
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
#include <ThreadManagementHelpers.h>
#include <MainNotificationRegistration.h>
#include <Warnings.h>
#include <ForkWarningHelpers.h>
#include <BlockInvalidationHelpers.h>
#include <I_ChainTipManager.h>

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
bool fImporting = false;
bool fReindex = false;
bool fVerifyingBlocks = false;


bool IsFinalTx(const CTransaction& tx, const CChain& activeChain, int nBlockHeight = 0 , int64_t nBlockTime = 0);

CCheckpointServices checkpointsVerifier(GetCurrentChainCheckpoints);


std::map<uint256, int64_t> mapRejectedBlocks;
// Internal stuff
namespace
{
/**
     * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
     * as good as our current tip or better. Entries may be failed, though.
     */


/**
     * Sources of received blocks, to be able to send them reject messages or ban
     * them, if processing happens afterwards. Protected by cs_main.
     */
std::map<uint256, NodeId> mapBlockSource;

} // anon namespace

static bool UpdateDBIndicesForNewBlock(
    const IndexDatabaseUpdates& indexDatabaseUpdates,
    const uint256& bestBlockHash,
    CBlockTreeDB& blockTreeDatabase,
    CValidationState& state)
{
    if (blockTreeDatabase.GetTxIndexing())
        if (!blockTreeDatabase.WriteTxIndex(indexDatabaseUpdates.txLocationData))
            return state.Abort("ConnectingBlock: Failed to write transaction index");

    if (indexDatabaseUpdates.addressIndexingEnabled_) {
        if (!blockTreeDatabase.WriteAddressIndex(indexDatabaseUpdates.addressIndex)) {
            return state.Abort("ConnectingBlock: Failed to write address index");
        }

        if (!blockTreeDatabase.UpdateAddressUnspentIndex(indexDatabaseUpdates.addressUnspentIndex)) {
            return state.Abort("ConnectingBlock: Failed to write address unspent index");
        }
    }

    if (indexDatabaseUpdates.spentIndexingEnabled_)
        if (!blockTreeDatabase.UpdateSpentIndex(indexDatabaseUpdates.spentIndex))
            return state.Abort("ConnectingBlock: Failed to write update spent index");

    return blockTreeDatabase.WriteBestBlockHash(bestBlockHash);
}
//////////////////////////////////////////////////////////////////////////////
//
// dispatching functions
//

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

    GetMainNotificationInterface().SyncTransactions(std::vector<CTransaction>({tx}), NULL,TransactionSyncType::MEMPOOL_TX_ADD);

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

bool FindBlockPos(CValidationState& state, CDiskBlockPos& pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown = false)
{
    if(fKnown) return BlockFileHelpers::FindKnownBlockPos(state,pos,nAddSize,nHeight,nTime);
    else return BlockFileHelpers::FindUnknownBlockPos(state,pos,nAddSize,nHeight,nTime);
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

bool ConnectBlock(
    const CBlock& block,
    CValidationState& state,
    CBlockIndex* pindex,
    ChainstateManager& chainstate,
    const CSporkManager& sporkManager,
    CCoinsViewCache& view,
    bool fJustCheck,
    bool fAlreadyChecked)
{
    AssertLockHeld(cs_main);

    // Check it again in case a previous version let a bad block in
    if (!fAlreadyChecked && !CheckBlock(block, state))
        return false;

    const CChainParams& chainParameters = Params();
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

    const SuperblockSubsidyContainer subsidiesContainer(chainParameters, sporkManager);
    const BlockIncentivesPopulator incentives(
        chainParameters,
        GetMasternodeModule(),
        subsidiesContainer.superblockHeightValidator(),
        subsidiesContainer.blockSubsidiesProvider());

    IndexDatabaseUpdates indexDatabaseUpdates(
        chainstate.BlockTree().GetAddressIndexing(),
        chainstate.BlockTree().GetSpentIndexing());
    CBlockRewards nExpectedMint = subsidiesContainer.blockSubsidiesProvider().GetBlockSubsidity(pindex->nHeight);
    if(ActivationState(pindex->pprev).IsActive(Fork::DeprecateMasternodes))
    {
        nExpectedMint.nStakeReward += nExpectedMint.nMasternodeReward;
        nExpectedMint.nMasternodeReward = 0;
    }
    BlockTransactionChecker blockTxChecker(block, state, pindex, view, chainstate.GetBlockMap());

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
           !UpdateDBIndicesForNewBlock(indexDatabaseUpdates, pindex->GetBlockHash(), chainstate.BlockTree(), state))
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
            ((mode == FLUSH_STATE_PERIODIC || mode == FLUSH_STATE_IF_NEEDED) && coinsTip.GetCacheSize() > chainstate->GetNominalViewCacheSize() ) ||
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
                GetMainNotificationInterface().SetBestChain(chainstate->ActiveChain().GetLocator());
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
            std::string warningMessage = translate("Warning: This version is obsolete, upgrade required!");
            Warnings::setMiscWarning(warningMessage);
            CAlert::Notify(settings,warningMessage, true);
            fWarned = true;
        }
    }
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
bool static ConnectTip(
    ChainstateManager& chainstate,
    const CSporkManager& sporkManager,
     CValidationState& state,
     CBlockIndex* pindexNew,
     const CBlock* pblock,
     bool fAlreadyChecked)
{
    AssertLockHeld(cs_main);

    auto& coinsTip = chainstate.CoinsTip();
    const auto& blockMap = chainstate.GetBlockMap();

    assert(pindexNew->pprev == chainstate.ActiveChain().Tip());
    CTxMemPool& mempool = GetTransactionMemoryPool();
    mempool.check(&coinsTip, blockMap);
    CCoinsViewCache view(&coinsTip);

    assert(pblock || !fAlreadyChecked);

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
        bool rv = ConnectBlock(*pblock, state, pindexNew, chainstate, sporkManager, view, false, fAlreadyChecked);
        if (!rv) {
            if (state.IsInvalid())
                InvalidBlockFound(mapBlockSource,IsInitialBlockDownload(),settings,cs_main,pindexNew, state);
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
    GetMainNotificationInterface().SyncTransactions(conflictedTransactions, NULL,TransactionSyncType::CONFLICTED_TX);
    // ... and about transactions that got confirmed:
    GetMainNotificationInterface().SyncTransactions(pblock->vtx, pblock, TransactionSyncType::NEW_BLOCK);

    int64_t nTime6 = GetTimeMicros();
    nTimePostConnect += nTime6 - nTime5;
    nTimeTotal += nTime6 - nTime1;
    LogPrint("bench", "  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001, nTimePostConnect * 0.000001);
    LogPrint("bench", "- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);
    return true;
}

class ChainTipManager final: public I_ChainTipManager
{
private:
    const CSporkManager& sporkManager_;
    ChainstateManager& chainstate_;
    const bool defaultBlockChecking_;
    CValidationState& state_;
    const bool updateCoinDatabaseOnly_;
    const BlockDiskDataReader blockDiskReader_;
    const BlockConnectionService blockConnectionService_;
public:
    ChainTipManager(
        const CSporkManager& sporkManager,
        ChainstateManager& chainstate,
        const bool defaultBlockChecking,
        CValidationState& state,
        const bool updateCoinDatabaseOnly
        ): sporkManager_(sporkManager)
        , chainstate_(chainstate)
        , defaultBlockChecking_(defaultBlockChecking)
        , state_(state)
        , updateCoinDatabaseOnly_(updateCoinDatabaseOnly)
        , blockDiskReader_()
        , blockConnectionService_(&chainstate_.BlockTree(), &chainstate_.CoinsTip(), blockDiskReader_,false)
    {}

    bool connectTip(const CBlock* pblock, CBlockIndex* blockIndex) const override
    {
        return ConnectTip(chainstate_, sporkManager_, state_, blockIndex, pblock, (!pblock)? false: defaultBlockChecking_ );
    }
    bool disconnectTip() const override
    {
        AssertLockHeld(cs_main);

        auto& coinsTip = chainstate_.CoinsTip();
        const auto& blockMap = chainstate_.GetBlockMap();
        const auto& chain = chainstate_.ActiveChain();

        const CBlockIndex* pindexDelete = chain.Tip();
        assert(pindexDelete);
        CTxMemPool& mempool = GetTransactionMemoryPool();
        mempool.check(&coinsTip, blockMap);
        // Read block from disk.
        std::pair<CBlock,bool> disconnectedBlock =
            blockConnectionService_.DisconnectBlock(state_, pindexDelete, updateCoinDatabaseOnly_);
        if(!disconnectedBlock.second)
            return error("%s : DisconnectBlock %s failed", __func__, pindexDelete->GetBlockHash());
        std::vector<CTransaction>& blockTransactions = disconnectedBlock.first.vtx;

        // Write the chain state to disk, if necessary.
        if (!FlushStateToDisk(state_, FLUSH_STATE_ALWAYS))
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
        GetMainNotificationInterface().SyncTransactions(blockTransactions, NULL,TransactionSyncType::BLOCK_DISCONNECT);
        return true;
    }
};

class I_MostWorkChainTipLocator
{
public:
    virtual ~I_MostWorkChainTipLocator(){}
    virtual CBlockIndex* findMostWorkChain() const = 0;
};

class I_MostWorkChainTransitionMediator: public I_MostWorkChainTipLocator
{
public:
    virtual ~I_MostWorkChainTransitionMediator(){}
    virtual bool transitionActiveChainToMostWorkChain(
            CBlockIndex* pindexMostWork,
            const CBlock* pblock) const = 0;
};
class MostWorkChainTransitionMediator final: public I_MostWorkChainTransitionMediator
{
private:
    ChainstateManager& chainstate_;
    BlockIndexSuccessorsByPreviousBlockIndex& unlinkedBlocks_;
    BlockIndexCandidates& blockIndexCandidates_;
    CValidationState& state_;
    const I_ChainTipManager& chainTipManager_;

    void computeNextBlockIndicesToConnect(
        CBlockIndex* pindexMostWork,
        const int startingHeight,
        const int maxHeightTarget,
        std::vector<CBlockIndex*>& blockIndicesToConnect) const
    {
        blockIndicesToConnect.clear();
        CBlockIndex* pindexIter = pindexMostWork->GetAncestor(maxHeightTarget);
        while (pindexIter && pindexIter->nHeight != startingHeight) {
            blockIndicesToConnect.push_back(pindexIter);
            pindexIter = pindexIter->pprev;
        }
    }

    bool rollBackChainTipToConnectToMostWorkChain(
        const CChain& chain,
        const CBlockIndex* mostWorkBlockIndex) const
    {
        const CBlockIndex* pindexFork = chain.FindFork(mostWorkBlockIndex);
        // Disconnect active blocks which are no longer in the best chain.
        while (chain.Tip() && chain.Tip() != pindexFork) {
            if (!chainTipManager_.disconnectTip())
                return false;
        }
        return true;
    }

    /**
     * Make the best chain active, in multiple steps. The result is either failure
     * or an activated best chain. pblock is either NULL or a pointer to a block
     * that is already loaded (to avoid loading it again from disk).
     */
    bool checkBlockConnectionState(
        CBlockIndex* lastBlockIndex) const
    {
        if (state_.IsInvalid())
        {
            // The block violates a consensus rule.
            if (!state_.CorruptionPossible())
                InvalidChainFound(IsInitialBlockDownload(),settings,cs_main,lastBlockIndex);
            state_ = CValidationState();
            return false;
        }
        else
        {
            return true;
        }
    }

    enum class BlockConnectionResult
    {
        TRY_NEXT_BLOCK,
        UNKNOWN_SYSTEM_ERROR,
        INVALID_BLOCK,
        CHAINWORK_IMPROVED,
    };

    BlockConnectionResult tryToConnectNextBlock(
        const CChain& chain,
        const CBlock* blockToConnect,
        const CBlockIndex* previousChainTip,
        CBlockIndex* proposedNewChainTip,
        CBlockIndex* pindexConnect) const
    {
        const bool blockSuccessfullyConnected = chainTipManager_.connectTip(blockToConnect,pindexConnect);
        if (!blockSuccessfullyConnected)
        {
            if(!checkBlockConnectionState(proposedNewChainTip))
            {
                return BlockConnectionResult::INVALID_BLOCK;
            }
            else
            {
                // A system error occurred (disk space, database error, ...)
                return BlockConnectionResult::UNKNOWN_SYSTEM_ERROR;
            }
        } else {
            PruneBlockIndexCandidates(chain);
            if (!previousChainTip || chain.Tip()->nChainWork > previousChainTip->nChainWork) {
                // We're in a better position than we were. Return temporarily to release the lock.
                return BlockConnectionResult::CHAINWORK_IMPROVED;
            }
            return BlockConnectionResult::TRY_NEXT_BLOCK;
        }
    }


public:
    MostWorkChainTransitionMediator(
        ChainstateManager& chainstate,
        BlockIndexSuccessorsByPreviousBlockIndex& unlinkedBlocks,
        BlockIndexCandidates& blockIndexCandidates,
        CValidationState& state,
        const I_ChainTipManager& chainTipManager
        ): chainstate_(chainstate)
        , unlinkedBlocks_(unlinkedBlocks)
        , blockIndexCandidates_(blockIndexCandidates)
        , state_(state)
        , chainTipManager_(chainTipManager)
    {
    }

    bool transitionActiveChainToMostWorkChain(
        CBlockIndex* pindexMostWork,
        const CBlock* pblock) const
    {
        AssertLockHeld(cs_main);

        const auto& chain = chainstate_.ActiveChain();
        const CBlockIndex* previousChainTip = chain.Tip();

        // Disconnect active blocks which are no longer in the best chain.
        if(!rollBackChainTipToConnectToMostWorkChain(chain, pindexMostWork)) return false;
        const CBlockIndex* rolledBackChainTip = chain.Tip();
        int nHeight = rolledBackChainTip? rolledBackChainTip->nHeight : -1;

        // Build list of new blocks to connect.
        std::vector<CBlockIndex*> blockIndicesToConnect;
        blockIndicesToConnect.reserve(32);
        BlockConnectionResult result = BlockConnectionResult::TRY_NEXT_BLOCK;
        while (nHeight != pindexMostWork->nHeight) {
            // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
            // a few blocks along the way.
            int nTargetHeight = std::min(nHeight + 32, pindexMostWork->nHeight);
            computeNextBlockIndicesToConnect(pindexMostWork,nHeight,nTargetHeight,blockIndicesToConnect);
            nHeight = nTargetHeight;

            // Connect new blocks.
            for(std::vector<CBlockIndex*>::reverse_iterator it = blockIndicesToConnect.rbegin();
                it != blockIndicesToConnect.rend() && result == BlockConnectionResult::TRY_NEXT_BLOCK;
                ++it)
            {
                CBlockIndex* pindexConnect = *it;
                const CBlock* blockToConnect = pindexConnect == pindexMostWork ? pblock : nullptr;
                result = tryToConnectNextBlock(
                    chain, blockToConnect, previousChainTip,blockIndicesToConnect.back(),pindexConnect);
            }
            if(result != BlockConnectionResult::TRY_NEXT_BLOCK) break;
        }
        if(result == BlockConnectionResult::UNKNOWN_SYSTEM_ERROR) return false;

        // Callbacks/notifications for a new best chain.
        if (result == BlockConnectionResult::INVALID_BLOCK)
            CheckForkWarningConditionsOnNewFork(settings, cs_main,blockIndicesToConnect.back(), IsInitialBlockDownload());
        else
            CheckForkWarningConditions(settings, cs_main,IsInitialBlockDownload());

        return true;
    }
    CBlockIndex* findMostWorkChain() const
    {
        return FindMostWorkChain(chainstate_, unlinkedBlocks_, blockIndexCandidates_);
    }
};

bool ActivateBestChainTemp(
    const I_MostWorkChainTransitionMediator& chainTransitionMediator,
    ChainstateManager& chainstate,
    const CBlock* pblock)
{
    const auto& chain = chainstate.ActiveChain();

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

            pindexMostWork = chainTransitionMediator.findMostWorkChain();

            // Whether we have anything to do at all.
            if (pindexMostWork == NULL || pindexMostWork == chain.Tip())
                return true;

            const CBlock* connectingBlock = (pblock && pblock->GetHash() == pindexMostWork->GetBlockHash())? pblock : nullptr;
            if (!chainTransitionMediator.transitionActiveChainToMostWorkChain(pindexMostWork, connectingBlock))
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
            GetMainNotificationInterface().UpdatedBlockTip(pindexNewTip);
            timeOfLastChainTipUpdate = GetTime();
        }
    } while (pindexMostWork != chain.Tip());

    return true;
}

bool ActivateBestChain(
    ChainstateManager& chainstate,
    const CSporkManager& sporkManager,
    CValidationState& state,
    const CBlock* pblock,
    bool fAlreadyChecked)
{
    ChainTipManager chainTipManager(sporkManager,chainstate,fAlreadyChecked,state,false);
    MostWorkChainTransitionMediator chainTransitionMediator(chainstate, GetBlockIndexSuccessorsByPreviousBlockIndex(), GetBlockIndexCandidates(), state,chainTipManager);
    const bool result = ActivateBestChainTemp(chainTransitionMediator, chainstate, pblock);

    // Write changes periodically to disk, after relay.
    if (!FlushStateToDisk(state, FLUSH_STATE_PERIODIC)) {
        return false;
    }
    VerifyBlockIndexTree(chainstate,cs_main, GetBlockIndexSuccessorsByPreviousBlockIndex(), GetBlockIndexCandidates());
    return result;
}

bool InvalidateBlock(ChainstateManager& chainstate, CValidationState& state, CBlockIndex* pindex, const bool updateCoinDatabaseOnly)
{
    ChainTipManager chainTipManager(GetSporkManager(),chainstate,true,state,updateCoinDatabaseOnly);
    return InvalidateBlock(chainTipManager, IsInitialBlockDownload(), settings, cs_main, chainstate, pindex);
}

bool ReconsiderBlock(ChainstateManager& chainstate, CValidationState& state, CBlockIndex* pindex)
{
    AssertLockHeld(cs_main);

    const auto& chain = chainstate.ActiveChain();

    int nHeight = pindex->nHeight;

    // Remove the invalidity flag from this block and all its descendants.
    for (auto& entry : chainstate.GetBlockMap()) {
        CBlockIndex& blk = *entry.second;
        if (!blk.IsValid() && blk.GetAncestor(nHeight) == pindex) {
            blk.nStatus &= ~BLOCK_FAILED_MASK;
            BlockFileHelpers::RecordDirtyBlockIndex(&blk);
            if (blk.IsValid(BLOCK_VALID_TRANSACTIONS) && blk.nChainTx && GetBlockIndexCandidates().value_comp()(chain.Tip(), &blk)) {
                GetBlockIndexCandidates().insert(&blk);
            }
            updateMostWorkInvalidBlockIndex(&blk, true);
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
    ChainstateManager::Reference chainstate;
    auto& blockMap = chainstate->GetBlockMap();

    const auto& sporkManager = GetSporkManager();
    const BlockIndexLotteryUpdater lotteryUpdater(Params(), chainstate->ActiveChain(), sporkManager);
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
bool ReceivedBlockTransactions(const CBlock& block, CBlockIndex* pindexNew, const CDiskBlockPos& pos)
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
    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();
    UpdateBlockCandidatesAndSuccessors(chain,pindexNew);
    return true;
}

bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, const CBlockIndex* const pindexPrev)
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

bool ContextualCheckBlock(const CBlock& block, CValidationState& state, const CBlockIndex* const pindexPrev)
{
    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    const int nHeight = pindexPrev == NULL ? 0 : pindexPrev->nHeight + 1;

    // Check that all transactions are finalized
    for (const auto& tx : block.vtx)
    {
        if (!IsFinalTx(tx, chain, nHeight, block.GetBlockTime()))
        {
            return state.DoS(10, error("%s : contains a non-final transaction", __func__), REJECT_INVALID, "bad-txns-nonfinal");
        }
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

namespace
{

bool AcceptBlockHeader(const CBlock& block, ChainstateManager& chainstate, const CSporkManager& sporkManager, CValidationState& state, CBlockIndex** ppindex)
{
    AssertLockHeld(cs_main);

    const auto& blockMap = chainstate.GetBlockMap();

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
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK)
        {
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

bool AcceptBlock(CBlock& block, ChainstateManager& chainstate, const CSporkManager& sporkManager, CValidationState& state, CBlockIndex** ppindex, CDiskBlockPos* dbp, bool fAlreadyCheckedBlock)
{
    AssertLockHeld(cs_main);

    const auto& blockMap = chainstate.GetBlockMap();

    CBlockIndex*& pindex = *ppindex;

    // Get prev block index
    CBlockIndex* pindexPrev = NULL;
    if (block.GetHash() != Params().HashGenesisBlock()) {
        const auto mi = blockMap.find(block.hashPrevBlock);
        if (mi == blockMap.end())
            return state.DoS(0, error("%s : prev block %s not found", __func__, block.hashPrevBlock), 0, "bad-prevblk");
        pindexPrev = (*mi).second;
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK)
        {
            return state.DoS(100, error("%s : prev block %s is invalid, unable to add block %s", __func__, block.hashPrevBlock, block.GetHash()),
                             REJECT_INVALID, "bad-prevblk");
        }
    }

    const ProofOfStakeModule posModule(Params(), chainstate.ActiveChain(), blockMap);
    const I_ProofOfStakeGenerator& posGenerator = posModule.proofOfStakeGenerator();

    const uint256 blockHash = block.GetHash();
    if (blockHash != Params().HashGenesisBlock())
    {
        if(!CheckWork(Params(), posGenerator, blockMap, settings, block, mapProofOfStake, pindexPrev))
        {
            LogPrintf("WARNING: %s: check difficulty check failed for %s block %s\n",__func__, block.IsProofOfWork()?"PoW":"PoS", blockHash);
            return false;
        }
    }

    if (!AcceptBlockHeader(block, chainstate, sporkManager, state, &pindex))
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
        if (!ReceivedBlockTransactions(block, pindex, blockPos))
            return error("AcceptBlock() : ReceivedBlockTransactions failed");
    } catch (std::runtime_error& e) {
        return state.Abort(std::string("System error: ") + e.what());
    }

    return true;
}

} // anonymous namespace

class ChainExtensionService final: public I_ChainExtensionService
{
public:
    virtual bool assignBlockIndex(
        CBlock& block,
        ChainstateManager& chainstate,
        const CSporkManager& sporkManager,
        CValidationState& state,
        CBlockIndex** ppindex,
        CDiskBlockPos* dbp,
        bool fAlreadyCheckedBlock) const override
    {
        return AcceptBlock(block,chainstate,sporkManager,state,ppindex,dbp,fAlreadyCheckedBlock);
    }

    virtual bool updateActiveChain(
        ChainstateManager& chainstate,
        const CSporkManager& sporkManager,
        CValidationState& state,
        const CBlock* pblock,
        bool fAlreadyChecked) const override
    {
        return ActivateBestChain(chainstate,sporkManager,state,pblock,fAlreadyChecked);
    }
};

bool ProcessNewBlock(ChainstateManager& chainstate, const CSporkManager& sporkManager, CValidationState& state, CNode* pfrom, CBlock* pblock, CDiskBlockPos* dbp)
{
    // Preliminary checks
    int64_t nStartTime = GetTimeMillis();

    ChainExtensionService chainExtensionService;
    AcceptBlockValidator blockValidator(chainExtensionService, cs_main, Params(), mapBlockSource, chainstate,sporkManager,state,pfrom, dbp);
    bool checked = true;
    if(!blockValidator.checkBlockRequirements(*pblock,checked)) return false;

    std::pair<CBlockIndex*,bool> assignedBlockIndex = blockValidator.validateAndAssignBlockIndex(*pblock,checked);
    if(!assignedBlockIndex.second) return false;
    CBlockIndex* pindex = assignedBlockIndex.first;
    assert(pindex != nullptr);

    if(!blockValidator.connectActiveChain(pindex,*pblock,checked)) return false;

    VoteForMasternodePayee(pindex);
    VerifyBlockIndexTree(chainstate,cs_main,GetBlockIndexSuccessorsByPreviousBlockIndex(),GetBlockIndexCandidates());
    LogPrintf("%s : ACCEPTED in %ld milliseconds with size=%d\n", __func__, GetTimeMillis() - nStartTime,
              pblock->GetSerializeSize(SER_DISK, CLIENT_VERSION));

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

static void InitializeBlockIndexGlobalData(BlockMap& blockIndicesByHash)
{
    const std::vector<std::pair<int, CBlockIndex*> > heightSortedBlockIndices = ComputeHeightSortedBlockIndices(blockIndicesByHash);
    auto& blockIndexCandidates = GetBlockIndexCandidates();
    auto& blockIndexSuccessorsByPrevBlockIndex = GetBlockIndexSuccessorsByPreviousBlockIndex();
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
                    blockIndexSuccessorsByPrevBlockIndex.insert(std::make_pair(pindex->pprev, pindex));
                }
            } else {
                pindex->nChainTx = pindex->nTx;
            }
        }
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && (pindex->nChainTx || pindex->pprev == NULL))
            blockIndexCandidates.insert(pindex);
        if (pindex->nStatus & BLOCK_FAILED_MASK)
            updateMostWorkInvalidBlockIndex(pindex);
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

bool static RollbackCoinDB(
    const CBlockIndex* const finalBlockIndex,
    const CBlockIndex* currentBlockIndex,
    CCoinsViewCache& view)
{
    BlockDiskDataReader blockDataReader;
    while(currentBlockIndex && finalBlockIndex->nHeight < currentBlockIndex->nHeight)
    {
        CBlock block;
        if (!blockDataReader.ReadBlock(currentBlockIndex,block))
            return error("%s: Unable to read block",__func__);

        CBlockUndo blockUndo;
        if(!blockDataReader.ReadBlockUndo(currentBlockIndex,blockUndo))
            return error("%s: failed to read block undo for %s", __func__, block.GetHash());

        for (int transactionIndex = block.vtx.size() - 1; transactionIndex >= 0; transactionIndex--)
        {
            const CTransaction& tx = block.vtx[transactionIndex];
            const TransactionLocationReference txLocationReference(tx, currentBlockIndex->nHeight, transactionIndex);
            const auto* undo = (transactionIndex > 0 ? &blockUndo.vtxundo[transactionIndex - 1] : nullptr);
            const TxReversalStatus status = view.UpdateWithReversedTransaction(tx,txLocationReference,undo);
            if(status != TxReversalStatus::OK)
            {
                return error("%s: unable to reverse transaction\n",__func__);
            }
        }
        view.SetBestBlock(currentBlockIndex->GetBlockHash());
        currentBlockIndex = currentBlockIndex->pprev;
    }
    assert(currentBlockIndex->GetBlockHash() == finalBlockIndex->GetBlockHash());
    return true;
}

bool static RollforwardkCoinDB(
    const CBlockIndex* const finalBlockIndex,
    const CBlockIndex* currentBlockIndex,
    CCoinsViewCache& view)
{
    std::vector<const CBlockIndex*> blocksToRollForward;
    int numberOfBlocksToRollforward = finalBlockIndex->nHeight - currentBlockIndex->nHeight;
    assert(numberOfBlocksToRollforward>=0);
    if(numberOfBlocksToRollforward < 1) return true;

    blocksToRollForward.resize(numberOfBlocksToRollforward);
    for(const CBlockIndex* rollbackBlock = finalBlockIndex;
        rollbackBlock && currentBlockIndex != rollbackBlock;
        rollbackBlock = rollbackBlock->pprev)
    {
        blocksToRollForward[--numberOfBlocksToRollforward] = rollbackBlock;
    }

    BlockDiskDataReader blockDataReader;
    for(const CBlockIndex* nextBlockIndex: blocksToRollForward)
    {
        CBlock block;
        if (!blockDataReader.ReadBlock(nextBlockIndex,block))
            return error("%s: Unable to read block",__func__);

        for(const CTransaction& tx: block.vtx)
        {
            if(!view.HaveInputs(tx)) return error("%s: unable to apply transction\n",__func__);
            CTxUndo undoDummy;
            view.UpdateWithConfirmedTransaction(tx, nextBlockIndex->nHeight, undoDummy);
        }
        view.SetBestBlock(nextBlockIndex->GetBlockHash());
    }
    assert(view.GetBestBlock() == finalBlockIndex->GetBlockHash());
    return true;
}

bool static ResolveConflictsBetweenCoinDBAndBlockDB(
    const BlockMap& blockMap,
    const uint256& bestBlockHashInBlockDB,
    CCoinsViewCache& coinsTip,
    std::string& strError)
{
    if (coinsTip.GetBestBlock() != bestBlockHashInBlockDB)
    {
        const auto mit = blockMap.find(coinsTip.GetBestBlock());
        if (mit == blockMap.end())
        {
            strError = "The wallet has been not been closed gracefully, causing the transaction database to be out of sync with the block database. Coin db best block unknown";;
            return false;
        }
        const auto iteratorToBestBlock = blockMap.find(bestBlockHashInBlockDB);
        if (iteratorToBestBlock == blockMap.end())
        {
            strError = "The wallet has been not been closed gracefully, causing the transaction database to be out of sync with the block database. Block db best block unknown";
            return false;
        }
        const CBlockIndex* coinDBBestBlockIndex = mit->second;
        const CBlockIndex* blockDBBestBlockIndex = iteratorToBestBlock->second;
        const CBlockIndex* const lastCommonSyncedBlockIndex = LastCommonAncestor(coinDBBestBlockIndex,blockDBBestBlockIndex);

        const int coinsHeight = coinDBBestBlockIndex->nHeight;
        const int blockIndexHeight = blockDBBestBlockIndex->nHeight;
        LogPrintf("%s : pcoinstip synced to block height %d, block index height %d, last common synced height: %d\n",
             __func__, coinsHeight, blockIndexHeight, lastCommonSyncedBlockIndex->nHeight);

        CCoinsViewCache view(&coinsTip);
        if(!RollbackCoinDB(lastCommonSyncedBlockIndex,coinDBBestBlockIndex,view))
        {
            return error("%s: unable to roll back coin db\n",__func__);
        }
        if(!RollforwardkCoinDB(blockDBBestBlockIndex,lastCommonSyncedBlockIndex,view))
        {
            return error("%s: unable to roll forward coin db\n",__func__);
        }
        // Save the updates to disk
        if(!view.Flush())
            return error("%s: unable to flush coin db ammendments to coinsTip\n",__func__);
        if (!coinsTip.Flush())
            LogPrintf("%s : unable to flush coinTip to disk\n", __func__);

        //get the index associated with the point in the chain that pcoinsTip is synced to
        LogPrintf("%s : pcoinstip=%d %s\n", __func__, coinsHeight, coinsTip.GetBestBlock());
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
    InitializeBlockIndexGlobalData(blockMap);

    // Load block file info
    BlockFileHelpers::ReadBlockFiles(blockTree);

    // Check presence of blk files
    if(!VerifyAllBlockFilesArePresent(blockMap)) return error("Some block files that were expected to be found are missing!");

    //Check if the shutdown procedure was followed on last client exit
    if(settings.ParameterIsSet("-safe_shutdown"))
    {
        blockTree.WriteFlag("shutdown", settings.GetBoolArg("-safe_shutdown",true));
    }
    bool fLastShutdownWasPrepared = true;
    blockTree.ReadFlag("shutdown", fLastShutdownWasPrepared);
    LogPrintf("%s: Last shutdown was prepared: %s\n", __func__, fLastShutdownWasPrepared);

    //Check for inconsistency with block file info and internal state
    if (!fLastShutdownWasPrepared && !settings.GetBoolArg("-forcestart", false) && !settings.GetBoolArg("-reindex", false))
    {
        uint256 expectedBestBlockHash;
        if(!blockTree.ReadBestBlockHash(expectedBestBlockHash) || !ResolveConflictsBetweenCoinDBAndBlockDB(blockMap,expectedBestBlockHash,coinsTip,strError))
        {
            return false;
        }
        if(settings.ParameterIsSet("-safe_shutdown"))
            assert(coinsTip.GetBestBlock() == expectedBestBlockHash && "Coin database and block database have inconsistent best block");
    }

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    blockTree.ReadReindexing(fReindexing);
    fReindex |= fReindexing;

    // Check whether we have address, spent or tx indexing enabled
    blockTree.LoadIndexingFlags();

    // If this is written true before the next client init, then we know the shutdown process failed
    blockTree.WriteFlag("shutdown", false);

    // Load pointer to end of best chain
    const auto it = blockMap.find(coinsTip.GetBestBlock());
    if (it == blockMap.end())
        return true;
    chain.SetTip(it->second);

    PruneBlockIndexCandidates(chain);

    LogPrintf("%s: hashBestChain=%s height=%d date=%s progress=%f\n",
            __func__,
            chain.Tip()->GetBlockHash(), chain.Height(),
            DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chain.Tip()->GetBlockTime()),
            checkpointsVerifier.GuessVerificationProgress(chain.Tip()));

    return true;
}

void UnloadBlockIndex(ChainstateManager* chainstate)
{
    GetBlockIndexCandidates().clear();
    updateMostWorkInvalidBlockIndex(nullptr);

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


bool InitBlockIndex(ChainstateManager& chainstate, const CSporkManager& sporkManager)
{
    LOCK(cs_main);

    auto& blockTree = chainstate.BlockTree();

    // Check whether we're already initialized
    if (chainstate.ActiveChain().Genesis() != nullptr)
        return true;

    // Use the provided setting for transaciton search indices
    blockTree.WriteIndexingFlags(
        settings.GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX),
        settings.GetBoolArg("-spentindex", DEFAULT_SPENTINDEX),
        settings.GetBoolArg("-txindex", true)
    );


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
            if (!ReceivedBlockTransactions(block, pindex, blockPos))
                return error("LoadBlockIndex() : genesis block not accepted");
            if (!ActivateBestChain(chainstate, sporkManager, state, &block))
                return error("LoadBlockIndex() : genesis block cannot be activated");
            // Force a chainstate write so that when we VerifyDB in a moment, it doesnt check stale data
            return FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
        } catch (std::runtime_error& e) {
            return error("LoadBlockIndex() : failed to initialize block database: %s", e.what());
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////////
//
// CAlert
//


//////////////////////////////////////////////////////////////////////////////
//
// Messages
//


bool static AlreadyHave(const CTxMemPool& mempool, const CInv& inv)
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
            if (GetTransactionMemoryPool().lookup(inv.GetHash(), tx))
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
    if (pfrom->GetVersion() != 0) {
        pfrom->PushMessage("reject", lastCommand, REJECT_DUPLICATE, string("Duplicate version message"));
        Misbehaving(pfrom->GetNodeState(), 1,"Duplicated version message");
        return false;
    }

    int64_t nTime;
    CAddress addrMe;
    CAddress addrFrom;
    uint64_t nNonce = 1;

    int nodeVersion;
    uint64_t bitmaskOfNodeServices;
    vRecv >> nodeVersion >> bitmaskOfNodeServices >> nTime >> addrMe;
    pfrom->SetVersionAndServices(nodeVersion,bitmaskOfNodeServices);
    if (pfrom->DisconnectOldProtocol(ActiveProtocol(), lastCommand))
    {
        PeerBanningService::Ban(GetTime(),pfrom->GetCAddress());
        return false;
    }

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
        LogPrintf("connected to self at %s, disconnecting\n", pfrom->GetCAddress());
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

    pfrom->fClient = !(pfrom->GetServices() & NODE_NETWORK);

    // Potentially mark this peer as a preferred download peer.
    pfrom->UpdatePreferredDownloadStatus();

    // Change version
    pfrom->PushMessage("verack");

    if(pfrom->fInbound) {
        pfrom->PushMessage("sporkcount", GetSporkManager().GetActiveSporkCount());
    }

    pfrom->SetOutboundSerializationVersion(min(pfrom->GetVersion(), PROTOCOL_VERSION));

    if (!pfrom->fInbound) {
        // Advertise our address
        if (IsListening() && !IsInitialBlockDownload()) {
            CAddress addr = GetLocalAddress(&pfrom->GetCAddress());
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
        if (pfrom->fOneShot || pfrom->GetVersion() >= CADDR_TIME_VERSION || addrman.size() < 1000) {
            pfrom->PushMessage("getaddr");
            pfrom->fGetAddr = true;
        }
        addrman.Good(pfrom->GetCAddress());
    } else {
        if (((CNetAddr)pfrom->GetCAddress()) == (CNetAddr)addrFrom) {
            addrman.Add(addrFrom, addrFrom);
            addrman.Good(addrFrom);
        }
    }

    // Relay alerts
    RelayAllAlertsTo(pfrom);

    pfrom->RecordSuccessfullConnection();

    string remoteAddr;
    if (ShouldLogPeerIPs())
        remoteAddr = ", peeraddr=" + pfrom->GetCAddress().ToString();

    LogPrintf("receive version message: %s: version %d, blocks=%d, us=%s, peer=%d%s\n",
                pfrom->cleanSubVer, pfrom->GetVersion(),
                pfrom->nStartingHeight, addrMe, pfrom->id,
                remoteAddr);

    AddTimeData(pfrom->GetCAddress(), nTime);
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

    ChainstateManager::Reference chainstate;
    const auto& coinsTip = chainstate->CoinsTip();
    const auto& blockMap = chainstate->GetBlockMap();
    const auto& chain = chainstate->ActiveChain();
    auto& sporkManager = GetSporkManager ();

    if (strCommand == std::string(NetworkMessageType_VERSION))
    {
        if(!SetPeerVersionAndServices(pfrom,addrman,vRecv))
        {
            return false;
        }
        return true;
    }
    else if (pfrom->GetVersion() == 0)
    {
        // Must have a version message before anything else
        Misbehaving(pfrom->GetNodeState(), 1,"Version message has not arrived before other handshake steps");
        return false;
    }
    else if (strCommand == "verack")
    {
        pfrom->SetInboundSerializationVersion(min(pfrom->GetVersion(), PROTOCOL_VERSION));

        // Mark this node as currently connected, so we update its timestamp later.
        if (!pfrom->fInbound) {
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
        if (pfrom->GetVersion() < CADDR_TIME_VERSION && addrman.size() > 1000)
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
        addrman.Add(vAddrOk, pfrom->GetCAddress(), 2 * 60 * 60);
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

            bool fAlreadyHave = AlreadyHave(GetTransactionMemoryPool(), inv);
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

        pfrom->HandleRequestForData(vInv);
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

        CTxMemPool& mempool = GetTransactionMemoryPool();
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
            if (!AcceptBlockHeader((CBlock)header, *chainstate, sporkManager, state, &pindexLast)) {
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

        VerifyBlockIndexTree(*chainstate,cs_main,GetBlockIndexSuccessorsByPreviousBlockIndex(),GetBlockIndexCandidates());
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
                ProcessNewBlock(*chainstate, sporkManager, state, pfrom, &block);
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
                    PeerBanningService::Ban(GetTime(),pfrom->GetCAddress());
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
        if (settings.debugModeIsEnabled()) {
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
        sporkManager.ProcessSpork(cs_main, pfrom, strCommand, vRecv);
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
        LogPrintf("Warning: not punishing whitelisted peer %s!\n", pto->GetCAddress());
    }
    else
    {
        pto->FlagForDisconnection();
        if (pto->GetCAddress().IsLocal())
        {
            LogPrintf("Warning: not banning local peer %s!\n", pto->GetCAddress());
        }
        else
        {
            PeerBanningService::Ban(GetTime(),pto->GetCAddress());
        }
    }
    nodeState->fShouldBan = false;
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
void CollectNonBlockDataToRequestAndRequestIt(const CTxMemPool& mempool, CNode* pto, int64_t nNow, std::vector<CInv>& vGetData)
{
    while (!pto->IsFlaggedForDisconnection() && !pto->mapAskFor.empty() && (*pto->mapAskFor.begin()).first <= nNow)
    {
        const CInv& inv = (*pto->mapAskFor.begin()).second;
        if (!AlreadyHave(mempool, inv)) {
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

void RebroadcastSomeMempoolTxs(CTxMemPool& mempool)
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

void PeriodicallyRebroadcastMempoolTxs(CTxMemPool& mempool)
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
        RebroadcastSomeMempoolTxs(mempool);
}

bool SendMessages(CNode* pto, bool fSendTrickle)
{
    const ChainstateManager::Reference chainstate;

    {
        if (fSendTrickle) {
            SendAddresses(pto);
        }

        CheckForBanAndDisconnectIfNotWhitelisted(pto);

        if (pindexBestHeader == nullptr)
            pindexBestHeader = chainstate->ActiveChain().Tip();

        // Start block sync
        const CNodeState* state = pto->GetNodeState();
        bool fFetch = state->fPreferredDownload || (!CNodeState::HavePreferredDownloadPeers() && !pto->fClient && !pto->fOneShot); // Download if this is a nice peer, or we have no nice peers and this one might do.
        if(fFetch)
        {
            BeginSyncingWithPeer(pto);
        }
        CTxMemPool& mempool = GetTransactionMemoryPool();
        if(!fReindex) PeriodicallyRebroadcastMempoolTxs(mempool);
        SendInventoryToPeer(pto,fSendTrickle);
        int64_t nNow = GetTimeMicros();
        std::vector<CInv> vGetData;
        {
            LOCK(cs_main);
            RequestDisconnectionFromNodeIfStalling(nNow,pto);
            if(fFetch) CollectBlockDataToRequest(nNow,pto,vGetData);
        }
        CollectNonBlockDataToRequestAndRequestIt(mempool, pto,nNow,vGetData);
    }
    return true;
}

int GetBestHeaderBlockHeight()
{
    return pindexBestHeader? pindexBestHeader->nHeight: -1;
}
