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

struct IndexDatabaseUpdates
{
    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> > spentIndex;
};

ActiveChainManager::ActiveChainManager(
    const bool& addressIndexingIsEnabled,
    const bool& spentInputIndexingIsEnabled,
    CBlockTreeDB* blocktree
    ): addressIndexingIsEnabled_(addressIndexingIsEnabled)
    , spentInputIndexingIsEnabled_(spentInputIndexingIsEnabled)
    , blocktree_(blocktree)
{
}

bool ActiveChainManager::UpdateIndexDBs(
    IndexDatabaseUpdates& indexDBUpdates,
    CValidationState& state)
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

static void CollectIndexUpdatesFromOutputs(
    const CTransaction& tx,
    const uint256& hash,
    CBlockIndex* pindex,
    const int transactionIndex,
    IndexDatabaseUpdates& indexDBUpdates)
{
    std::vector<std::pair<CAddressIndexKey, CAmount> >& addressIndex = indexDBUpdates.addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >& addressUnspentIndex = indexDBUpdates.addressUnspentIndex;
    for (unsigned int k = tx.vout.size(); k-- > 0;)
    {
        const CTxOut &out = tx.vout[k];

        if (out.scriptPubKey.IsPayToScriptHash())
        {
            std::vector<unsigned char> hashBytes(out.scriptPubKey.begin()+2, out.scriptPubKey.begin()+22);
            // undo receiving activity
            addressIndex.push_back(std::make_pair(CAddressIndexKey(2, uint160(hashBytes), pindex->nHeight, transactionIndex, hash, k, false), out.nValue));
            // undo unspent index
            addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(2, uint160(hashBytes), hash, k), CAddressUnspentValue()));

        }
        else if (out.scriptPubKey.IsPayToPublicKeyHash())
        {
            std::vector<unsigned char> hashBytes(out.scriptPubKey.begin()+3, out.scriptPubKey.begin()+23);
            // undo receiving activity
            addressIndex.push_back(std::make_pair(CAddressIndexKey(1, uint160(hashBytes), pindex->nHeight, transactionIndex, hash, k, false), out.nValue));
            // undo unspent index
            addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(1, uint160(hashBytes), hash, k), CAddressUnspentValue()));

        }
    }
}

static void CollectIndexUpdatesFromInputs(
    const bool addressIndexingIsEnabled_,
    const bool spentInputIndexingIsEnabled_,
    CCoinsViewCache& view,
    const CTransaction& tx,
    const uint256& hash,
    CBlockIndex* pindex,
    const int transactionIndex,
    const int txOutputIndex,
    const CTxInUndo& undo,
    IndexDatabaseUpdates& indexDBUpdates)
{
    const CTxIn input = tx.vin[txOutputIndex];
    if (spentInputIndexingIsEnabled_)
    {
        // undo and delete the spent index
        indexDBUpdates.spentIndex.push_back(std::make_pair(CSpentIndexKey(input.prevout.hash, input.prevout.n), CSpentIndexValue()));
    }

    if (addressIndexingIsEnabled_)
    {
        const CTxOut &prevout = view.GetOutputFor(tx.vin[txOutputIndex]);
        if (prevout.scriptPubKey.IsPayToScriptHash()) {
            std::vector<unsigned char> hashBytes(prevout.scriptPubKey.begin()+2, prevout.scriptPubKey.begin()+22);
            // undo spending activity
            indexDBUpdates.addressIndex.push_back(std::make_pair(CAddressIndexKey(2, uint160(hashBytes), pindex->nHeight, transactionIndex, hash, txOutputIndex, true), prevout.nValue * -1));
            // restore unspent index
            indexDBUpdates.addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(2, uint160(hashBytes), input.prevout.hash, input.prevout.n), CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, undo.nHeight)));
        }
        else if (prevout.scriptPubKey.IsPayToPublicKeyHash())
        {
            std::vector<unsigned char> hashBytes(prevout.scriptPubKey.begin()+3, prevout.scriptPubKey.begin()+23);
            // undo spending activity
            indexDBUpdates.addressIndex.push_back(std::make_pair(CAddressIndexKey(1, uint160(hashBytes), pindex->nHeight, transactionIndex, hash, txOutputIndex, true), prevout.nValue * -1));
            // restore unspent index
            indexDBUpdates.addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(1, uint160(hashBytes), input.prevout.hash, input.prevout.n), CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, undo.nHeight)));
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
    const uint256& hash,
    CCoinsViewCache& view,
    CBlockIndex* pindex)
{
    bool outputsAvailable = true;
    // Check that all outputs are available and match the outputs in the block itself
    // exactly. Note that transactions with only provably unspendable outputs won't
    // have outputs available even in the block itself, so we handle that case
    // specially with outsEmpty.
    CCoins outsEmpty;
    CCoinsModifier outs = view.ModifyCoins(hash);
    outs->ClearUnspendable();

    CCoins outsBlock(tx, pindex->nHeight);
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
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull())
        return error("DisconnectBlock() : no undo data available");
    if (!blockUndo.ReadFromDisk(pos, pindex->pprev->GetBlockHash()))
        return error("DisconnectBlock() : failure reading undo data");

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size())
        return error("DisconnectBlock() : block and undo data inconsistent");

    bool fClean = true;
    IndexDatabaseUpdates indexDBUpdates;

    // undo transactions in reverse order
    for (int transactionIndex = block.vtx.size() - 1; transactionIndex >= 0; transactionIndex--) {
        const CTransaction& tx = block.vtx[transactionIndex];

        uint256 hash = tx.GetHash();

        if (addressIndexingIsEnabled_)
        {
            CollectIndexUpdatesFromOutputs(tx,hash,pindex,transactionIndex,indexDBUpdates);
        }
        fClean = fClean && CheckTxOutputsAreAvailable(tx,hash,view,pindex);

        // restore inputs
        if (!tx.IsCoinBase() )
        {
            const CTxUndo& txundo = blockUndo.vtxundo[transactionIndex - 1];
            if (txundo.vprevout.size() != tx.vin.size())
                return error("DisconnectBlock() : transaction and undo data inconsistent - txundo.vprevout.siz=%d tx.vin.siz=%d", txundo.vprevout.size(), tx.vin.size());

            for (unsigned int txOutputIndex = tx.vin.size(); txOutputIndex-- > 0;)
            {
                const COutPoint& out = tx.vin[txOutputIndex].prevout;
                const CTxInUndo& undo = txundo.vprevout[txOutputIndex];
                CCoinsModifier coins = view.ModifyCoins(out.hash);
                UpdateCoinsForRestoredInputs(out,undo,coins,fClean);
                CollectIndexUpdatesFromInputs(
                    addressIndexingIsEnabled_,
                    spentInputIndexingIsEnabled_,
                    view,
                    tx,
                    hash,
                    pindex,
                    transactionIndex,
                    txOutputIndex,
                    undo,
                    indexDBUpdates);
            }
        }
    }

    // move best block pointer to prevout block
    view.SetBestBlock(pindex->pprev->GetBlockHash());

    if (pfClean) {
        *pfClean = fClean;
        return true;
    } else {
        if(!UpdateIndexDBs(indexDBUpdates,state))
        {
            return false;
        }

        return fClean;
    }
}