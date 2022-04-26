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
#include <UtxoCheckingAndUpdating.h>

ActiveChainManager::ActiveChainManager(
    const bool& addressIndexingIsEnabled,
    const bool& spentIndexingIsEnabled,
    CBlockTreeDB* blocktree,
    const I_BlockDataReader& blockDataReader
    ): addressIndexingIsEnabled_(addressIndexingIsEnabled)
    , spentIndexingIsEnabled_(spentIndexingIsEnabled)
    , blocktree_(blocktree)
    , blockDataReader_(blockDataReader)
{
}

bool ActiveChainManager::ApplyDisconnectionUpdateIndexToDBs(
    const uint256& bestBlockHash,
    IndexDatabaseUpdates& indexDBUpdates,
    CValidationState& state) const
{
    if (addressIndexingIsEnabled_) {
        if (!blocktree_->EraseAddressIndex(indexDBUpdates.addressIndex)) {
            return state.Abort("Disconnecting block: Failed to delete address index");
        }
        if (!blocktree_->UpdateAddressUnspentIndex(indexDBUpdates.addressUnspentIndex)) {
            return state.Abort("Disconnecting block: Failed to write address unspent index");
        }
    }
    if(spentIndexingIsEnabled_)
    {
        if (!blocktree_->UpdateSpentIndex(indexDBUpdates.spentIndex)) {
            return state.Abort("Disconnecting block: Failed to write update spent index");
        }
    }
    return blocktree_->WriteBestBlockHash(bestBlockHash);
}


static bool CheckTxReversalStatus(const TxReversalStatus status, bool& fClean)
{
    if(status == TxReversalStatus::ABORT_NO_OTHER_ERRORS)
    {
        return false;
    }
    else if (status == TxReversalStatus::ABORT_WITH_OTHER_ERRORS)
    {
        fClean = false;
        return false;
    }
    else if (status == TxReversalStatus::CONTINUE_WITH_ERRORS)
    {
        fClean = false;
    }
    return true;
}
/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  If fJustCheck is false, then updates to the address and spent indices are written
 *  to disk.  The coins view is always updated.
 *  Returns true on success and false if some error or inconsistency was discovered.  */
bool ActiveChainManager::DisconnectBlock(
    const CBlock& block,
    CValidationState& state,
    const CBlockIndex* pindex,
    CCoinsViewCache& view,
    const bool fJustCheck) const
{
    if (pindex->GetBlockHash() != view.GetBestBlock())
        LogPrintf("%s : pindex=%s view=%s\n", __func__, pindex->GetBlockHash(), view.GetBestBlock());
    assert(pindex->GetBlockHash() == view.GetBestBlock());

    CBlockUndo blockUndo;
    if(!blockDataReader_.ReadBlockUndo(pindex,blockUndo))
        return error("%s: failed to read block undo for %s", __func__, block.GetHash());
    if(blockUndo.vtxundo.size() + 1 != block.vtx.size())
        return error("%s: block and undo data inconsistent", __func__);

    IndexDatabaseUpdates indexDBUpdates(addressIndexingIsEnabled_,spentIndexingIsEnabled_);
    // undo transactions in reverse order
    for (int transactionIndex = block.vtx.size() - 1; transactionIndex >= 0; transactionIndex--) {
        const CTransaction& tx = block.vtx[transactionIndex];
        const TransactionLocationReference txLocationReference(tx, pindex->nHeight, transactionIndex);
        const auto* undo = (transactionIndex > 0 ? &blockUndo.vtxundo[transactionIndex - 1] : nullptr);
        const TxReversalStatus status = view.UpdateWithReversedTransaction(tx,txLocationReference,undo);

        bool fClean;
        if (!CheckTxReversalStatus(status, fClean))
            return error("%s: error reverting transaction %s in block %s at height %d",
                         __func__, tx.GetHash(), block.GetHash(), pindex->nHeight);

        if(!fJustCheck)
            IndexDatabaseUpdateCollector::ReverseTransaction(tx,txLocationReference,view,indexDBUpdates);
    }

    // undo transactions in reverse order
    view.SetBestBlock(pindex->pprev->GetBlockHash());
    if(!fJustCheck)
    {
        if(!ApplyDisconnectionUpdateIndexToDBs(pindex->pprev->GetBlockHash(), indexDBUpdates,state))
            return error("%s: failed to apply index updates for block %s", __func__, block.GetHash());
    }

    return true;
}

void ActiveChainManager::DisconnectBlock(
    std::pair<CBlock,bool>& disconnectedBlockAndStatus,
    CValidationState& state,
    const CBlockIndex* pindex,
    CCoinsViewCache& coins,
    const bool updateCoinsCacheOnly) const
{
    CBlock& block = disconnectedBlockAndStatus.first;
    bool& status = disconnectedBlockAndStatus.second;
    if (!blockDataReader_.ReadBlock(pindex,block))
    {
        status = state.Abort("Failed to read block");
        return;
    }
    int64_t nStart = GetTimeMicros();
    status = DisconnectBlock(block, state, pindex, coins, updateCoinsCacheOnly);
    LogPrint("bench", "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);
}
