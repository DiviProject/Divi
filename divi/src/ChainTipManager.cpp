#include <ChainTipManager.h>

#include <list>

#include <Settings.h>
#include <sync.h>
#include <BlockDiskDataReader.h>
#include <BlockConnectionService.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <chain.h>
#include <blockmap.h>
#include <txmempool.h>
#include <FlushChainState.h>
#include <NotificationInterface.h>
#include <ChainstateManager.h>
#include <ValidationState.h>
#include <ChainSyncHelpers.h>
#include <Logging.h>
#include <alert.h>
#include <Warnings.h>
#include <utiltime.h>
#include <BlockInvalidationHelpers.h>

extern bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree, bool* pfMissingInputs = nullptr, bool ignoreFees = false);

namespace
{

/** Update chainActive and related internal data structures. */
void static UpdateTip(const CBlockIndex* pindexNew, CCriticalSection& mainCriticalSection, const Settings& settings)
{
    ChainstateManager::Reference chainstate;
    auto& chain = chainstate->ActiveChain();
    chain.SetTip(pindexNew);

    // New best block
    LogPrintf("%s: new best=%s  height=%d  log2_work=%.8g  tx=%lu  date=%s cache=%u\n", __func__,
              chain.Tip()->GetBlockHash(), chain.Height(), log(chain.Tip()->nChainWork.getdouble()) / log(2.0), (unsigned long)chain.Tip()->nChainTx,
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chain.Tip()->GetBlockTime()),
               (unsigned int)chainstate->CoinsTip().GetCacheSize());


    // Check the version of the last 100 blocks to see if we need to upgrade:
    static bool fWarned = false;
    if (!IsInitialBlockDownload(mainCriticalSection,settings) && !fWarned) {
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
            std::string warningMessage = "Warning: This version is obsolete, upgrade required!";
            Warnings::setMiscWarning(warningMessage);
            CAlert::Notify(settings,warningMessage, true);
            fWarned = true;
        }
    }
}

} // namespace


ChainTipManager::ChainTipManager(
    const CChainParams& chainParameters,
    const Settings& settings,
    CCriticalSection& mainCriticalSection,
    CTxMemPool& mempool,
    MainNotificationSignals& mainNotificationSignals,
    const MasternodeModule& masternodeModule,
    std::map<uint256, NodeId>& peerIdByBlockHash,
    const CSporkManager& sporkManager,
    ChainstateManager& chainstate
    ): settings_(settings)
    , mainCriticalSection_(mainCriticalSection)
    , mempool_(mempool)
    , mainNotificationSignals_(mainNotificationSignals)
    , peerIdByBlockHash_(peerIdByBlockHash)
    , sporkManager_(sporkManager)
    , chainstate_(chainstate)
    , blockDiskReader_(new BlockDiskDataReader() )
    , blockConnectionService_(
        new BlockConnectionService(
            chainParameters,
            masternodeModule,
            chainstate_.GetBlockMap(),
            &chainstate_.BlockTree(),
            &chainstate_.CoinsTip(),
            sporkManager_,
            *blockDiskReader_,
            false))
{}

ChainTipManager::~ChainTipManager()
{
    blockConnectionService_.reset();
    blockDiskReader_.reset();
}

bool ChainTipManager::connectTip(CValidationState& state,const CBlock* pblock, CBlockIndex* blockIndex) const
{
    AssertLockHeld(mainCriticalSection_);
    auto& coinsTip = chainstate_.CoinsTip();
    const auto& blockMap = chainstate_.GetBlockMap();

    assert(blockIndex->pprev == chainstate_.ActiveChain().Tip());
    mempool_.check(&coinsTip, blockMap);

    // Read block from disk.
    CBlock block;
    if (!pblock) {
        if (!blockDiskReader_->ReadBlock(blockIndex,block))
            return state.Abort("Failed to read block");
        pblock = &block;
    }
    // Apply the block atomically to the chain state.
    {
        bool rv = blockConnectionService_->ConnectBlock(*pblock,state,blockIndex,false,false);
        if (!rv) {
            if (state.IsInvalid())
                InvalidBlockFound(peerIdByBlockHash_,IsInitialBlockDownload(mainCriticalSection_,settings_),settings_,mainCriticalSection_,blockIndex, state);
            return error("%s : ConnectBlock %s failed",__func__, blockIndex->GetBlockHash());
        }
        peerIdByBlockHash_.erase(blockIndex->GetBlockHash());
    }

    // Write the chain state to disk, if necessary. Always write to disk if this is the first of a new file.
    FlushStateMode flushMode = FLUSH_STATE_IF_NEEDED;
    if (blockIndex->pprev && (blockIndex->GetBlockPos().nFile != blockIndex->pprev->GetBlockPos().nFile))
        flushMode = FLUSH_STATE_ALWAYS;
    if (!FlushStateToDisk(state, flushMode,mainNotificationSignals_,mainCriticalSection_))
        return false;

    // Remove conflicting transactions from the mempool.
    std::list<CTransaction> txConflicted;
    mempool_.removeConfirmedTransactions(pblock->vtx, blockIndex->nHeight, txConflicted);
    mempool_.check(&coinsTip, blockMap);
    // Update chainActive & related variables.
    UpdateTip(blockIndex,mainCriticalSection_,settings_);
    // Tell wallet about transactions that went from mempool
    // to conflicted:
    std::vector<CTransaction> conflictedTransactions(txConflicted.begin(),txConflicted.end());
    mainNotificationSignals_.SyncTransactions(conflictedTransactions, NULL,TransactionSyncType::CONFLICTED_TX);
    // ... and about transactions that got confirmed:
    mainNotificationSignals_.SyncTransactions(pblock->vtx, pblock, TransactionSyncType::NEW_BLOCK);

    return true;
}
bool ChainTipManager::disconnectTip(CValidationState& state, const bool updateCoinDatabaseOnly) const
{
    AssertLockHeld(mainCriticalSection_);

    auto& coinsTip = chainstate_.CoinsTip();
    const auto& blockMap = chainstate_.GetBlockMap();
    const auto& chain = chainstate_.ActiveChain();

    const CBlockIndex* pindexDelete = chain.Tip();
    assert(pindexDelete);
    mempool_.check(&coinsTip, blockMap);
    // Read block from disk.
    std::pair<CBlock,bool> disconnectedBlock =
        blockConnectionService_->DisconnectBlock(state, pindexDelete, updateCoinDatabaseOnly);
    if(!disconnectedBlock.second)
        return error("%s : DisconnectBlock %s failed", __func__, pindexDelete->GetBlockHash());
    std::vector<CTransaction>& blockTransactions = disconnectedBlock.first.vtx;

    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_ALWAYS, mainNotificationSignals_,mainCriticalSection_))
        return false;
    // Resurrect mempool transactions from the disconnected block.
    for(const CTransaction& tx: blockTransactions) {
        // ignore validation errors in resurrected transactions
        std::list<CTransaction> removed;
        CValidationState stateDummy;
        if (tx.IsCoinBase() || tx.IsCoinStake() || !AcceptToMemoryPool(mempool_, stateDummy, tx, false))
            mempool_.remove(tx, removed, true);
    }
    mempool_.removeCoinbaseSpends(&coinsTip, pindexDelete->nHeight);
    mempool_.check(&coinsTip, blockMap);
    // Update chainActive and related variables.
    UpdateTip(pindexDelete->pprev, mainCriticalSection_, settings_);
    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    mainNotificationSignals_.SyncTransactions(blockTransactions, NULL,TransactionSyncType::BLOCK_DISCONNECT);
    return true;
}