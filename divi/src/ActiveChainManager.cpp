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
        LogPrintf("%s : pindex=%s view=%s\n", __func__, pindex->GetBlockHash(), view.GetBestBlock());
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
        const TxReversalStatus status = UpdateCoinsReversingTransaction(tx,view,blockUndo.vtxundo[transactionIndex-1],pindex->nHeight);
        if(!CheckTxReversalStatus(status,fClean))
        {
            return false;
        }
        if(!pfClean)
        {
            TransactionLocationReference txLocationReference(tx.GetHash(),pindex->nHeight,transactionIndex);
            IndexDatabaseUpdateCollector::ReverseTransaction(tx,txLocationReference,view,indexDBUpdates);
        }
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
