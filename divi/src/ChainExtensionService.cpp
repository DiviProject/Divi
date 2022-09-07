#include <ChainExtensionService.h>

#include <FlushChainState.h>
#include <utiltime.h>
#include <ChainTipManager.h>
#include <MostWorkChainTransitionMediator.h>
#include <ChainSyncHelpers.h>
#include <pow.h>
#include <chainparams.h>
#include <ValidationState.h>
#include <Logging.h>
#include <chain.h>
#include <blockmap.h>
#include <ProofOfStakeModule.h>
#include <BlockFileHelpers.h>
#include <sync.h>
#include <BlockInvalidationHelpers.h>
#include <BlockDiskAccessor.h>
#include <TransactionFinalityHelpers.h>
#include <clientversion.h>
#include <Settings.h>
#include <NotificationInterface.h>
#include <txdb.h>
#include <BlockIndexLotteryUpdater.h>

extern void SetStakeModifiersForNewBlockIndex(const BlockMap& blockIndicesByHash, CBlockIndex* pindexNew);
namespace
{
std::map<uint256, uint256> mapProofOfStake;
CBlockIndex* AddToBlockIndex(
    ChainstateManager& chainstate,
    const CChainParams& chainParameters,
    const CSporkManager& sporkManager,
    const CBlock& block)
{
    auto& blockMap = chainstate.GetBlockMap();

    const BlockIndexLotteryUpdater lotteryUpdater(chainParameters, chainstate.ActiveChain(), sporkManager);
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
            LogPrintf("%s : SetStakeEntropyBit() failed \n",__func__);

        // ppcoin: record proof-of-stake hash value
        if (pindexNew->IsProofOfStake()) {
            if (!mapProofOfStake.count(hash))
                LogPrintf("%s : hashProofOfStake not found in map \n",__func__);
            pindexNew->hashProofOfStake = mapProofOfStake[hash];
        }

        // ppcoin: compute stake modifier
        SetStakeModifiersForNewBlockIndex(blockMap, pindexNew);
    }
    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);
    updateBestHeaderBlockIndex(pindexNew,true);

    //update previous block pointer
    if (pindexNew->nHeight)
        pindexNew->pprev->pnext = pindexNew;

    lotteryUpdater.UpdateBlockIndexLotteryWinners(block,pindexNew);

    BlockFileHelpers::RecordDirtyBlockIndex(pindexNew);

    return pindexNew;
}

bool ReceivedBlockTransactions(const CChain& chain, const CBlock& block, CBlockIndex* pindexNew, const CDiskBlockPos& pos)
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
    UpdateBlockCandidatesAndSuccessors(chain,pindexNew);
    return true;
}

bool FindBlockPos(CValidationState& state, CDiskBlockPos& pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown = false)
{
    if(fKnown) return BlockFileHelpers::FindKnownBlockPos(state,pos,nAddSize,nHeight,nTime);
    else return BlockFileHelpers::FindUnknownBlockPos(state,pos,nAddSize,nHeight,nTime);
}

bool ContextualCheckBlockHeader(
    const ChainstateManager& chainstate,
    const CChainParams& chainParameters,
    const Settings& settings,
    const CBlockHeader& block,
    CValidationState& state,
    const CBlockIndex* const pindexPrev)
{
    const uint256 hash = block.GetHash();

    if (hash == chainParameters.HashGenesisBlock())
        return true;

    assert(pindexPrev);
    int nHeight = pindexPrev->nHeight + 1;

    //If this is a reorg, check that it is not too deep
    int nMaxReorgDepth = settings.GetArg("-maxreorg", chainParameters.MaxReorganizationDepth());
    if (chainstate.ActiveChain().Height() - nHeight >= nMaxReorgDepth)
        return state.DoS(1, error("%s: forked chain older than max reorganization depth (height %d)", __func__, nHeight));

    // Check timestamp against prev
    if (block.GetBlockTime() <= pindexPrev->GetMedianTimePast()) {
        LogPrintf("Block time = %d , GetMedianTimePast = %d \n", block.GetBlockTime(), pindexPrev->GetMedianTimePast());
        return state.Invalid(error("%s : block's timestamp is too early", __func__),
                             REJECT_INVALID, "time-too-old");
    }

    // Check that the block chain matches the known block chain up to a checkpoint
    static const auto* blockHashCheckpointsByHeight = chainParameters.Checkpoints().mapCheckpoints;
    if(blockHashCheckpointsByHeight)
    {
        const auto it = blockHashCheckpointsByHeight->find(nHeight);
        if (it != blockHashCheckpointsByHeight->end() && it->second != hash)
            return state.DoS(100, error("%s : rejected by checkpoint lock-in at %d", __func__, nHeight),
                            REJECT_CHECKPOINT, "checkpoint mismatch");

        // Don't accept any forks from the main chain prior to last checkpoint
        const CBlockIndex* lastCheckpointRecordedInBlockMap = nullptr;
        const BlockMap& blockIndicesByBlockHash = chainstate.GetBlockMap();

        for(auto it = blockHashCheckpointsByHeight->rbegin(); it != blockHashCheckpointsByHeight->rend(); ++it)
        {
            const auto& checkpointBlockHashByHeight = *it;
            const uint256& hash = checkpointBlockHashByHeight.second;
            BlockMap::const_iterator locatedBlockIndex = blockIndicesByBlockHash.find(hash);
            if (locatedBlockIndex != blockIndicesByBlockHash.end())
                lastCheckpointRecordedInBlockMap = locatedBlockIndex->second;
        }
        if(lastCheckpointRecordedInBlockMap && lastCheckpointRecordedInBlockMap->nHeight > nHeight)
        {
            return state.DoS(0, error("%s : forked chain older than last checkpoint (height %d)", __func__, nHeight));
        }
    }

    // All blocks on DIVI later than the genesis block must be at least version 3.
    // (In fact they are version 4, but we only enforce version >=3 as this is what
    // the previous check based on BIP34 supermajority activation did.)
    if (block.nVersion < 3)
        return state.Invalid(error("%s : rejected nVersion=%d block", __func__, block.nVersion),
                             REJECT_OBSOLETE, "bad-version");

    return true;
}

bool ContextualCheckBlock(CCriticalSection& mainCriticalSection,const CChain& chain, const CBlock& block, CValidationState& state, const CBlockIndex* const pindexPrev)
{
    const int nHeight = pindexPrev == NULL ? 0 : pindexPrev->nHeight + 1;

    // Check that all transactions are finalized
    for (const auto& tx : block.vtx)
    {
        if (!IsFinalTx(mainCriticalSection,tx, chain, nHeight, block.GetBlockTime()))
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

bool AcceptBlockHeader(
    CCriticalSection& mainCriticalSection,
    const CChainParams& chainParameters,
    const Settings& settings,
    const CBlock& block,
    ChainstateManager& chainstate,
    const CSporkManager& sporkManager,
    CValidationState& state,
    CBlockIndex** ppindex)
{
    AssertLockHeld(mainCriticalSection);

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
    if (hash != chainParameters.HashGenesisBlock()) {
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

    if (!ContextualCheckBlockHeader(chainstate, chainParameters, settings,block, state, pindexPrev))
        return false;

    if (pindex == NULL)
        pindex = AddToBlockIndex(chainstate,chainParameters,sporkManager, block);

    if (ppindex)
        *ppindex = pindex;

    return true;
}

bool AcceptBlock(
    CCriticalSection& mainCriticalSection,
    const Settings& settings,
    const I_ProofOfStakeGenerator& posGenerator,
    const CChainParams& chainParameters,
    CBlock& block,
    ChainstateManager& chainstate,
    const CSporkManager& sporkManager,
    CValidationState& state,
    CBlockIndex** ppindex,
    CDiskBlockPos* dbp)
{
    AssertLockHeld(mainCriticalSection);

    const auto& blockMap = chainstate.GetBlockMap();

    CBlockIndex*& pindex = *ppindex;

    // Get prev block index
    CBlockIndex* pindexPrev = NULL;
    if (block.GetHash() != chainParameters.HashGenesisBlock()) {
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

    const uint256 blockHash = block.GetHash();
    if (blockHash != chainParameters.HashGenesisBlock())
    {
        if(!CheckWork(chainParameters, posGenerator, blockMap, settings, block, mapProofOfStake, pindexPrev))
        {
            LogPrintf("WARNING: %s: check difficulty check failed for %s block %s\n",__func__, block.IsProofOfWork()?"PoW":"PoS", blockHash);
            return false;
        }
    }

    if (!AcceptBlockHeader(mainCriticalSection, chainParameters, settings, block, chainstate, sporkManager, state, &pindex))
        return false;

    if (pindex->nStatus & BLOCK_HAVE_DATA) {
        // TODO: deal better with duplicate blocks.
        // return state.DoS(20, error("AcceptBlock() : already have block %d %s", pindex->nHeight, pindex->GetBlockHash()), REJECT_DUPLICATE, "duplicate");
        return true;
    }

    if (!ContextualCheckBlock(mainCriticalSection,chainstate.ActiveChain(), block, state, pindex->pprev))
    {
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
        if (!ReceivedBlockTransactions(chainstate.ActiveChain(), block, pindex, blockPos))
            return error("AcceptBlock() : ReceivedBlockTransactions failed");
    } catch (std::runtime_error& e) {
        return state.Abort(std::string("System error: ") + e.what());
    }

    return true;
}

}


bool ChainExtensionService::transitionToMostWorkChainTip(
    CValidationState& state,
    ChainstateManager& chainstate,
    const CBlock* pblock) const
{
    const auto& chain = chainstate.ActiveChain();

    const CBlockIndex* pindexNewTip = NULL;
    CBlockIndex* pindexMostWork = NULL;
    do {
        boost::this_thread::interruption_point();

        bool fInitialDownload;
        while (true) {
            TRY_LOCK(mainCriticalSection_, lockMain);
            if (!lockMain) {
                MilliSleep(50);
                continue;
            }

            pindexMostWork = chainTransitionMediator_->findMostWorkChain();

            // Whether we have anything to do at all.
            if (pindexMostWork == NULL || pindexMostWork == chain.Tip())
                return true;

            const CBlock* connectingBlock = (pblock && pblock->GetHash() == pindexMostWork->GetBlockHash())? pblock : nullptr;
            if (!chainTransitionMediator_->transitionActiveChainToMostWorkChain(state, pindexMostWork, connectingBlock))
                return false;

            pindexNewTip = chain.Tip();
            fInitialDownload = IsInitialBlockDownload(mainCriticalSection_,settings_);
            break;
        }
        // When we reach this point, we switched to a new tip (stored in pindexNewTip).

        // Notifications/callbacks that can run without cs_main
        if (!fInitialDownload) {
            // Notify external listeners about the new tip.
            mainNotificationSignals_.UpdatedBlockTip(pindexNewTip);
        }
    } while (pindexMostWork != chain.Tip());

    return true;
}

ChainExtensionService::ChainExtensionService(
    std::map<uint256, NodeId>& peerIdByBlockHash,
    ChainstateManager& chainstateManager,
    CTxMemPool& mempool,
    const MasternodeModule& masternodeModule,
    MainNotificationSignals& mainNotificationSignals,
    CCriticalSection& mainCriticalSection,
    const Settings& settings,
    const CChainParams& chainParameters,
    const CSporkManager& sporkManager,
    BlockIndexSuccessorsByPreviousBlockIndex& blockIndexSuccessors,
    BlockIndexCandidates& blockIndexCandidates
    ): mempool_(mempool)
    , masternodeModule_(masternodeModule)
    , mainNotificationSignals_(mainNotificationSignals)
    , mainCriticalSection_(mainCriticalSection)
    , settings_(settings)
    , chainParameters_(chainParameters)
    , sporkManager_(sporkManager)
    , chainstateRef_(&chainstateManager)
    , blockIndexSuccessors_(blockIndexSuccessors)
    , blockIndexCandidates_(blockIndexCandidates)
    , chainTipManager_(
        new ChainTipManager(
            chainParameters_,
            settings_,
            mainCriticalSection_,
            mempool_,
            mainNotificationSignals_,
            masternodeModule_,
            peerIdByBlockHash,
            sporkManager_,
            *chainstateRef_))
    , chainTransitionMediator_(
        new MostWorkChainTransitionMediator(
            settings_,
            mainCriticalSection_,
            *chainstateRef_,
            blockIndexSuccessors_,
            blockIndexCandidates_,
            *chainTipManager_))
    , posModule_( new ProofOfStakeModule(chainParameters, chainstateRef_->ActiveChain(), chainstateRef_->GetBlockMap()) )
{
}

ChainExtensionService::~ChainExtensionService()
{
    posModule_.reset();
    chainTransitionMediator_.reset();
    chainTipManager_.reset();
}

std::pair<CBlockIndex*, bool> ChainExtensionService::assignBlockIndex(
    CBlock& block,
    CValidationState& state,
    CDiskBlockPos* dbp) const
{
    std::pair<CBlockIndex*, bool> result(nullptr,false);
    result.second = AcceptBlock(
        mainCriticalSection_,settings_, posModule_->proofOfStakeGenerator(),chainParameters_,block,*chainstateRef_,sporkManager_,state,&result.first,dbp);
    return result;
}

bool ChainExtensionService::updateActiveChain(
    CValidationState& state,
    const CBlock* pblock) const
{
    const bool result = transitionToMostWorkChainTip(state, *chainstateRef_, pblock);
    // Write changes periodically to disk, after relay.
    if (!FlushStateToDisk(state, FLUSH_STATE_PERIODIC,mainNotificationSignals_,mainCriticalSection_)) {
        return false;
    }
    VerifyBlockIndexTree(*chainstateRef_,mainCriticalSection_, blockIndexSuccessors_, blockIndexCandidates_);
    return result;
}

bool ChainExtensionService::invalidateBlock(CValidationState& state, CBlockIndex* blockIndex, const bool updateCoinDatabaseOnly) const
{
    AssertLockHeld(mainCriticalSection_);
    return InvalidateBlock(*chainTipManager_, IsInitialBlockDownload(mainCriticalSection_,settings_), settings_, state, mainCriticalSection_, *chainstateRef_, blockIndex, updateCoinDatabaseOnly);
}

bool ChainExtensionService::reconsiderBlock(CValidationState& state, CBlockIndex* pindex) const
{
    AssertLockHeld(mainCriticalSection_);
    const auto& chain = chainstateRef_->ActiveChain();
    const int nHeight = pindex->nHeight;

    // Remove the invalidity flag from this block and all its descendants.
    for (auto& entry : chainstateRef_->GetBlockMap()) {
        CBlockIndex& blk = *entry.second;
        if (!blk.IsValid() && blk.GetAncestor(nHeight) == pindex) {
            blk.nStatus &= ~BLOCK_FAILED_MASK;
            BlockFileHelpers::RecordDirtyBlockIndex(&blk);
            if (blk.IsValid(BLOCK_VALID_TRANSACTIONS) && blk.nChainTx && blockIndexCandidates_.value_comp()(chain.Tip(), &blk)) {
                blockIndexCandidates_.insert(&blk);
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

bool ChainExtensionService::connectGenesisBlock() const
{
    LOCK(mainCriticalSection_);

    auto& blockTree = chainstateRef_->BlockTree();

    // Check whether we're already initialized
    if (chainstateRef_->ActiveChain().Genesis() != nullptr)
        return true;

    // Use the provided setting for transaciton search indices
    blockTree.WriteIndexingFlags(
        settings_.GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX),
        settings_.GetBoolArg("-spentindex", DEFAULT_SPENTINDEX),
        settings_.GetBoolArg("-txindex", true)
    );


    LogPrintf("Connecting genesis block...\n");

    // Only add the genesis block if not reindexing (in which case we reuse the one already on disk)
    if (!settings_.isReindexingBlocks()) {
        try {
            const CBlock& block = chainParameters_.GenesisBlock();
            // Start new block file
            unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
            CDiskBlockPos blockPos;
            CValidationState state;
            if (!FindBlockPos(state, blockPos, nBlockSize + 8, 0, block.GetBlockTime()))
                return error("%s : FindBlockPos failed",__func__);
            if (!WriteBlockToDisk(block, blockPos))
                return error("%s : writing genesis block to disk failed",__func__);
            CBlockIndex* pindex = AddToBlockIndex(*chainstateRef_, chainParameters_,sporkManager_,block);
            if (!ReceivedBlockTransactions(chainstateRef_->ActiveChain(),block, pindex, blockPos))
                return error("%s : genesis block not accepted",__func__);
            if (!updateActiveChain(state, &block))
                return error("%s : genesis block cannot be activated",__func__);
            // Force a chainstate write so that when we VerifyDB in a moment, it doesnt check stale data
            return FlushStateToDisk(state, FLUSH_STATE_ALWAYS,mainNotificationSignals_,mainCriticalSection_);
        } catch (std::runtime_error& e) {
            return error("%s : failed to initialize block database: %s", __func__, e.what());
        }
    }
    return true;
}