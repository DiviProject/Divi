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

struct IndexDatabaseUpdates
{
    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> > spentIndex;

    IndexDatabaseUpdates(
        ): addressIndex()
        , addressUnspentIndex()
        , spentIndex()
    {
    }
};

struct TransactionLocationReference
{
    uint256 hash;
    unsigned blockHeight;
    int transactionIndex;

    TransactionLocationReference(
        uint256 hashValue,
        unsigned blockheightValue,
        int transactionIndexValue
        ): hash(hashValue)
        , blockHeight(blockheightValue)
        , transactionIndex(transactionIndexValue)
    {
    }
};

ActiveChainManager::ActiveChainManager(
    const bool& addressIndexingIsEnabled,
    CBlockTreeDB* blocktree
    ): addressIndexingIsEnabled_(addressIndexingIsEnabled)
    , blocktree_(blocktree)
{
}

bool ActiveChainManager::ApplyUpdateIndexDBs(
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

void ActiveChainManager::CollectIndexUpdatesFromOutputs(
    const CTransaction& tx,
    const TransactionLocationReference& txLocationReference,
    IndexDatabaseUpdates& indexDBUpdates) const
{
    if (!addressIndexingIsEnabled_) return;
    const std::vector<CTxOut>& txOutputs = tx.vout;
    for (unsigned int k = txOutputs.size(); k-- > 0;)
    {
        const CTxOut &out = txOutputs[k];

        if (out.scriptPubKey.IsPayToScriptHash())
        {
            std::vector<unsigned char> hashBytes(out.scriptPubKey.begin()+2, out.scriptPubKey.begin()+22);
            // undo receiving activity
            indexDBUpdates.addressIndex.push_back(
                std::make_pair(
                    CAddressIndexKey(2, uint160(hashBytes), txLocationReference.blockHeight, txLocationReference.transactionIndex, txLocationReference.hash, k, false),
                    out.nValue));
            // undo unspent index
            indexDBUpdates.addressUnspentIndex.push_back(
                std::make_pair(
                    CAddressUnspentKey(2, uint160(hashBytes), txLocationReference.hash, k),
                    CAddressUnspentValue()));

        }
        else if (out.scriptPubKey.IsPayToPublicKeyHash())
        {
            std::vector<unsigned char> hashBytes(out.scriptPubKey.begin()+3, out.scriptPubKey.begin()+23);
            // undo receiving activity
            indexDBUpdates.addressIndex.push_back(
                std::make_pair(
                    CAddressIndexKey(1, uint160(hashBytes), txLocationReference.blockHeight, txLocationReference.transactionIndex, txLocationReference.hash, k, false),
                    out.nValue));
            // undo unspent index
            indexDBUpdates.addressUnspentIndex.push_back(
                std::make_pair(
                    CAddressUnspentKey(1, uint160(hashBytes), txLocationReference.hash, k),
                    CAddressUnspentValue()));

        }
    }
}

void ActiveChainManager::CollectIndexUpdatesFromInputs(
    CCoinsViewCache& view,
    const CTransaction& tx,
    const TransactionLocationReference& txLocationReference,
    const CTxUndo& txundo,
    IndexDatabaseUpdates& indexDBUpdates) const
{
    for( unsigned int txInputIndex = tx.vin.size(); txInputIndex-- > 0;)
    {
        const CTxInUndo& undo = txundo.vprevout[txInputIndex];
        const CTxIn& input = tx.vin[txInputIndex];
        if (addressIndexingIsEnabled_)
        {
            const CTxOut &prevout = view.GetOutputFor(input);
            if (prevout.scriptPubKey.IsPayToScriptHash()) {
                std::vector<unsigned char> hashBytes(prevout.scriptPubKey.begin()+2, prevout.scriptPubKey.begin()+22);
                // undo spending activity
                indexDBUpdates.addressIndex.push_back(
                    std::make_pair(
                        CAddressIndexKey(2, uint160(hashBytes), txLocationReference.blockHeight, txLocationReference.transactionIndex, txLocationReference.hash, txInputIndex, true),
                        prevout.nValue * -1));
                // restore unspent index
                indexDBUpdates.addressUnspentIndex.push_back(
                    std::make_pair(
                        CAddressUnspentKey(2, uint160(hashBytes), input.prevout.hash, input.prevout.n),
                        CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, undo.nHeight)));
            }
            else if (prevout.scriptPubKey.IsPayToPublicKeyHash())
            {
                std::vector<unsigned char> hashBytes(prevout.scriptPubKey.begin()+3, prevout.scriptPubKey.begin()+23);
                // undo spending activity
                indexDBUpdates.addressIndex.push_back(
                    std::make_pair(
                        CAddressIndexKey(1, uint160(hashBytes), txLocationReference.blockHeight, txLocationReference.transactionIndex, txLocationReference.hash, txInputIndex, true),
                        prevout.nValue * -1));
                // restore unspent index
                indexDBUpdates.addressUnspentIndex.push_back(
                    std::make_pair(
                        CAddressUnspentKey(1, uint160(hashBytes), input.prevout.hash, input.prevout.n),
                        CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, undo.nHeight)));
            }
        }
    }
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

bool ActiveChainManager::UpdateDBIndices(
    CBlock& block,
    CBlockUndo& blockUndo,
    CValidationState& state,
    CBlockIndex* pindex,
    CCoinsViewCache& view) const
{
    if(!addressIndexingIsEnabled_)
    {
        view.SetBestBlock(pindex->pprev->GetBlockHash());
        return true;
    }

    IndexDatabaseUpdates indexDBUpdates;
    for (int transactionIndex = block.vtx.size() - 1; transactionIndex >= 0; transactionIndex--) {
        const CTransaction& tx = block.vtx[transactionIndex];

        const uint256 hash = tx.GetHash();
        TransactionLocationReference txLocationReference(hash,pindex->nHeight,transactionIndex);
        CollectIndexUpdatesFromOutputs(tx,txLocationReference,indexDBUpdates);
        // restore inputs
        if (!tx.IsCoinBase() )
        {
            const CTxUndo& txundo = blockUndo.vtxundo[transactionIndex - 1];
            CollectIndexUpdatesFromInputs(view, tx, txLocationReference, txundo, indexDBUpdates);
        }
    }

    view.SetBestBlock(pindex->pprev->GetBlockHash());
    if(!ApplyUpdateIndexDBs(indexDBUpdates,state))
    {
        return false;
    }
    return true;
}


bool ActiveChainManager::RecoverBlockUndoData(const CBlockIndex* pindex, CBlockUndo& blockUndo) const
{
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull())
        return error("DisconnectBlock() : no undo data available");
    if (!blockUndo.ReadFromDisk(pos, pindex->pprev->GetBlockHash()))
        return error("DisconnectBlock() : failure reading undo data");

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
    if(!RecoverBlockUndoData(pindex,blockUndo))
    {
        return false;
    }
    else if(blockUndo.vtxundo.size() + 1 != block.vtx.size())
    {
        return error("DisconnectBlock() : block and undo data inconsistent");
    }

    bool fClean = true;
    // undo transactions in reverse order
    for (int transactionIndex = block.vtx.size() - 1; transactionIndex >= 0; transactionIndex--) {
        const CTransaction& tx = block.vtx[transactionIndex];

        const uint256 hash = tx.GetHash();
        TransactionLocationReference txLocationReference(hash,pindex->nHeight,transactionIndex);
        fClean = fClean && CheckTxOutputsAreAvailable(tx,txLocationReference,view);

        if (!tx.IsCoinBase() && !RestoreInputs(blockUndo,tx,transactionIndex,view,fClean))
        {
            return false;
        }
    }
    // undo transactions in reverse order
    if(!pfClean)
    {
        if(!UpdateDBIndices(block,blockUndo,state,pindex,view))
        {
            return false;
        }
        return fClean;
    }
    else if (pfClean)
    {
        view.SetBestBlock(pindex->pprev->GetBlockHash());
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
    if (!ReadBlockFromDisk(block, pindex))
    {
        status = state.Abort("Failed to read block");
        return;
    }
    int64_t nStart = GetTimeMicros();
    status = DisconnectBlock(block,state,pindex,coins);
    LogPrint("bench", "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);
}