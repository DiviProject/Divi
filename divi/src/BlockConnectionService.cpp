#include <BlockConnectionService.h>

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
#include <chainparams.h>
#include <SuperblockSubsidyContainer.h>
#include <BlockIncentivesPopulator.h>
#include <I_BlockDataReader.h>
#include <IndexDatabaseUpdates.h>
#include <IndexDatabaseUpdateCollector.h>
#include <UtxoCheckingAndUpdating.h>
#include <BlockCheckingHelpers.h>
#include <Logging.h>
#include <ForkActivation.h>
#include <BlockTransactionChecker.h>
#include <Settings.h>
#include <MasternodeModule.h>
#include <utilmoneystr.h>
#include <BlockFileHelpers.h>

extern Settings& settings;

namespace ConnectBlockHelpers
{
std::map<uint256, int64_t> mapRejectedBlocks;

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

bool UpdateDBIndicesForNewBlock(
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

}


BlockConnectionService::BlockConnectionService(
    const CChainParams& chainParameters,
    const MasternodeModule& masternodeModule,
    const BlockMap& blockIndicesByHash,
    CBlockTreeDB* blocktree,
    CCoinsViewCache* coinTip,
    const CSporkManager& sporkManager,
    const I_BlockDataReader& blockDataReader,
    const bool modifyCoinCacheInplace
    ): addressIndexingIsEnabled_(blocktree->GetAddressIndexing())
    , spentIndexingIsEnabled_(blocktree->GetSpentIndexing())
    , blockIndicesByHash_(blockIndicesByHash)
    , blocktree_(blocktree)
    , coinTip_(coinTip)
    , sporkManager_(sporkManager)
    , blockDataReader_(blockDataReader)
    , modifyCoinCacheInplace_(modifyCoinCacheInplace)
    , chainParameters_(chainParameters)
    , blockSubsidies_(new SuperblockSubsidyContainer(chainParameters_,sporkManager_))
    , incentives_(
        new BlockIncentivesPopulator(
            chainParameters_,
            masternodeModule,
            blockSubsidies_->superblockHeightValidator(),
            blockSubsidies_->blockSubsidiesProvider()))
{
}

BlockConnectionService::~BlockConnectionService()
{
    incentives_.reset();
    blockSubsidies_.reset();
}

bool BlockConnectionService::ApplyDisconnectionUpdateIndexToDBs(
    const uint256& bestBlockHash,
    IndexDatabaseUpdates& indexDBUpdates,
    CValidationState& state) const
{
    if (indexDBUpdates.addressIndexingEnabled_) {
        if (!blocktree_->EraseAddressIndex(indexDBUpdates.addressIndex)) {
            return state.Abort("Disconnecting block: Failed to delete address index");
        }
        if (!blocktree_->UpdateAddressUnspentIndex(indexDBUpdates.addressUnspentIndex)) {
            return state.Abort("Disconnecting block: Failed to write address unspent index");
        }
    }
    if(indexDBUpdates.addressIndexingEnabled_)
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
bool BlockConnectionService::DisconnectBlock(
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

std::pair<CBlock,bool> BlockConnectionService::DisconnectBlock(
    CValidationState& state,
    const CBlockIndex* pindex,
    const bool updateCoinsCacheOnly) const
{
    std::pair<CBlock,bool> disconnectedBlockAndStatus;
    CBlock& block = disconnectedBlockAndStatus.first;
    bool& status = disconnectedBlockAndStatus.second;
    if (!blockDataReader_.ReadBlock(pindex,block))
    {
        status = state.Abort("Failed to read block");
        return disconnectedBlockAndStatus;
    }
    int64_t nStart = GetTimeMicros();
    if(!modifyCoinCacheInplace_)
    {
        CCoinsViewCache coins(coinTip_);
        status = DisconnectBlock(block, state, pindex, coins, updateCoinsCacheOnly);
        if(status) assert(coins.Flush());
    }
    else
    {
        status = DisconnectBlock(block, state, pindex, *coinTip_, updateCoinsCacheOnly);
    }
    LogPrint("bench", "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);
    return disconnectedBlockAndStatus;
}

bool BlockConnectionService::ConnectBlock(
    const CBlock& block,
    CValidationState& state,
    CBlockIndex* pindex,
    const bool updateCoinsCacheOnly,
    const bool alreadyChecked) const
{
    CCoinsViewCache& view = *coinTip_;
    // Check it again in case a previous version let a bad block in
    if (!alreadyChecked && !CheckBlock(block, state))
        return false;

    const CChainParams& chainParameters = Params();
    ConnectBlockHelpers::VerifyBestBlockIsAtPreviousBlock(pindex,view);
    if (block.GetHash() == Params().HashGenesisBlock())
    {
        view.SetBestBlock(pindex->GetBlockHash());
        return true;
    }
    if(!ConnectBlockHelpers::CheckEnforcedPoSBlocksAndBIP30(chainParameters,block,state,pindex,view))
    {
        return false;
    }


    IndexDatabaseUpdates indexDatabaseUpdates(
        blocktree_->GetAddressIndexing(),
        blocktree_->GetSpentIndexing());
    CBlockRewards nExpectedMint = blockSubsidies_->blockSubsidiesProvider().GetBlockSubsidity(pindex->nHeight);
    if(ActivationState(pindex->pprev).IsActive(Fork::DeprecateMasternodes))
    {
        nExpectedMint.nStakeReward += nExpectedMint.nMasternodeReward;
        nExpectedMint.nMasternodeReward = 0;
    }
    BlockTransactionChecker blockTxChecker(block, state, pindex, view, blockIndicesByHash_);

    if(!blockTxChecker.Check(nExpectedMint, indexDatabaseUpdates))
    {
        return false;
    }
    ConnectBlockHelpers::CalculateFees(block.IsProofOfWork(),pindex,nExpectedMint);
    if (!ConnectBlockHelpers::CheckMintTotalsAndBlockPayees(block,pindex,*incentives_,nExpectedMint,state))
        return false;

    if (!settings.isStartupVerifyingBlocks()) {
        if (block.nAccumulatorCheckpoint != pindex->pprev->nAccumulatorCheckpoint)
            return state.DoS(100, error("%s : new accumulator checkpoint generated on a block that is not multiple of 10",__func__));
    }

    if (!blockTxChecker.WaitForScriptsToBeChecked())
        return state.DoS(100, false);

    if (!updateCoinsCacheOnly) {
        if(!ConnectBlockHelpers::WriteUndoDataToDisk(pindex,state,blockTxChecker.getBlockUndoData()) ||
           !ConnectBlockHelpers::UpdateDBIndicesForNewBlock(indexDatabaseUpdates, pindex->GetBlockHash(), *blocktree_, state))
        {
            return false;
        }
    }

    // add this block to the view's block chain
    view.SetBestBlock(pindex->GetBlockHash());

    return true;
}