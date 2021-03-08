#include <ActiveChainManager.h>

#include <primitives/block.h>
#include <ValidationState.h>
#include <chain.h>
#include <coins.h>
#include <BlockUndo.h>
#include <Logging.h>
#include <addressindex.h>
#include <txdb.h>
#include <spentindex.h>
#include <BlockDiskAccessor.h>
#include <utiltime.h>
#include <I_BlockDataReader.h>
#include <IndexDatabaseUpdates.h>
#include <IndexDatabaseUpdateCollector.h>

ActiveChainManager::ActiveChainManager(
    const bool& addressIndexingIsEnabled,
    CBlockTreeDB* blocktree,
    const I_BlockDataReader& blockDataReader
    ): addressIndexingIsEnabled_(addressIndexingIsEnabled)
    , blocktree_(blocktree)
    , blockDataReader_(blockDataReader)
{
}

bool ActiveChainManager::ApplyDisconnectionUpdateIndexToDBs(
    IndexDatabaseUpdates& indexDBUpdates,
    CValidationState& state) const
{
    if (addressIndexingIsEnabled_) {
        if (!blocktree_->EraseAddressIndex(indexDBUpdates.addressIndex)) {
            return state.Abort("Failed to delete address index");
        }
        if (!blocktree_->UpdateAddressUnspentIndex(indexDBUpdates.addressUnspentIndex)) {
            return state.Abort("Failed to write address unspent index");
        }
    }
    return true;
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

static bool CheckTxOutputsAreAvailable(
    const CTransaction& tx,
    const TransactionLocationReference& txLocationReference,
    CCoinsViewCache& view)
{
    bool outputsAvailable = true;
    // Check that all outputs are available and match the outputs in the block itself
    // exactly. Note that transactions with only provably unspendable outputs won't
    // have outputs available even in the block itself, so we handle that case
    // specially with outsEmpty.
    CCoins outsEmpty;
    CCoinsModifier outs = view.ModifyCoins(txLocationReference.hash);
    outs->ClearUnspendable();

    CCoins outsBlock(tx, txLocationReference.blockHeight);
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

static bool RestoreInputs(
    CBlockUndo& blockUndo,
    const CTransaction& tx,
    const int transactionIndex,
    CCoinsViewCache& view,
    bool& fClean)
{
    if(tx.IsCoinBase()) return true;
    const CTxUndo& txundo = blockUndo.vtxundo[transactionIndex - 1];
    if (txundo.vprevout.size() != tx.vin.size())
        return error("DisconnectBlock() : transaction and undo data inconsistent - txundo.vprevout.siz=%d tx.vin.siz=%d", txundo.vprevout.size(), tx.vin.size());

    for (unsigned int txInputIndex = tx.vin.size(); txInputIndex-- > 0;)
    {
        const COutPoint& out = tx.vin[txInputIndex].prevout;
        const CTxInUndo& undo = txundo.vprevout[txInputIndex];
        CCoinsModifier coins = view.ModifyCoins(out.hash);
        UpdateCoinsForRestoredInputs(out,undo,coins,fClean);
    }
    return true;
}

/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  In case pfClean is provided, operation will try to be tolerant about errors, and *pfClean
 *  will be true if no problems were found. Otherwise, the return value will be false in case
 *  of problems. Note that in any case, coins may be modified. */
bool ActiveChainManager::DisconnectBlock(
    CBlock& block,
    CValidationState& state,
    CBlockIndex* pindex,
    CCoinsViewCache& view,
    bool* pfClean) const
{
    if (pindex->GetBlockHash() != view.GetBestBlock())
        LogPrintf("%s : pindex=%s view=%s\n", __func__, pindex->GetBlockHash().GetHex(), view.GetBestBlock().GetHex());
    assert(pindex->GetBlockHash() == view.GetBestBlock());

    if (pfClean)
        *pfClean = false;

    CBlockUndo blockUndo;
    if(!blockDataReader_.ReadBlockUndo(pindex,blockUndo))
    {
        return false;
    }
    else if(blockUndo.vtxundo.size() + 1 != block.vtx.size())
    {
        return error("DisconnectBlock() : block and undo data inconsistent");
    }

    bool fClean = true;
    IndexDatabaseUpdates indexDBUpdates;
    // undo transactions in reverse order
    for (int transactionIndex = block.vtx.size() - 1; transactionIndex >= 0; transactionIndex--) {
        const CTransaction& tx = block.vtx[transactionIndex];

        TransactionLocationReference txLocationReference(tx.GetHash(),pindex->nHeight,transactionIndex);
        fClean = fClean && CheckTxOutputsAreAvailable(tx,txLocationReference,view);
        if (!RestoreInputs(blockUndo,tx,transactionIndex,view,fClean))
        {
            return false;
        }
        IndexDatabaseUpdateCollector::ReverseTransaction(tx,txLocationReference,view,indexDBUpdates);
    }
    // undo transactions in reverse order
    view.SetBestBlock(pindex->pprev->GetBlockHash());
    if(!pfClean)
    {
        if(!ApplyDisconnectionUpdateIndexToDBs(indexDBUpdates,state))
        {
            return false;
        }
        return fClean;
    }
    else
    {
        *pfClean = fClean;
        return true;
    }
}

void ActiveChainManager::DisconnectBlock(
    std::pair<CBlock,bool>& disconnectedBlockAndStatus,
    CValidationState& state,
    CBlockIndex* pindex,
    CCoinsViewCache& coins) const
{
    CBlock& block = disconnectedBlockAndStatus.first;
    bool& status = disconnectedBlockAndStatus.second;
    if (!blockDataReader_.ReadBlock(pindex,block))
    {
        status = state.Abort("Failed to read block");
        return;
    }
    int64_t nStart = GetTimeMicros();
    status = DisconnectBlock(block,state,pindex,coins);
    LogPrint("bench", "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);
}