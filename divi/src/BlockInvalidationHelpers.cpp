#include <BlockInvalidationHelpers.h>

#include <chain.h>
#include <blockmap.h>
#include <BlockFileHelpers.h>
#include <sync.h>
#include <ForkWarningHelpers.h>
#include <NodeStateRegistry.h>
#include <ChainstateManager.h>
#include <utiltime.h>
#include <Logging.h>
#include <I_ChainTipManager.h>
#include <ValidationState.h>

#include <deque>

/**
     * Every received block is assigned a unique and increasing identifier, so we
     * know which one to give priority in case of a fork.
     */
static CCriticalSection cs_nBlockSequenceId;
/** Blocks loaded from disk are assigned id 0, so start the counter at 1. */
static uint32_t nBlockSequenceId = 1;


static BlockIndexCandidates setBlockIndexCandidates;

/** All pairs A->B, where A (or one if its ancestors) misses transactions, but B has transactions. */
static BlockIndexSuccessorsByPreviousBlockIndex mapBlocksUnlinked;

BlockIndexCandidates& GetBlockIndexCandidates()
{
    return setBlockIndexCandidates;
}

BlockIndexSuccessorsByPreviousBlockIndex& GetBlockIndexSuccessorsByPreviousBlockIndex()
{
    return mapBlocksUnlinked;
}
void UpdateBlockCandidatesAndSuccessors(const CChain& chain, CBlockIndex* newlyConnectedBlockIndex)
{
    auto& blockIndexCandidates = GetBlockIndexCandidates();
    auto& blockIndexSuccessorsByPrevBlockIndex = GetBlockIndexSuccessorsByPreviousBlockIndex();
    if (newlyConnectedBlockIndex->pprev == NULL || newlyConnectedBlockIndex->pprev->nChainTx) {

        // If newlyConnectedBlockIndex is the genesis block or all parents are BLOCK_VALID_TRANSACTIONS.
        std::deque<CBlockIndex*> queue;
        queue.push_back(newlyConnectedBlockIndex);

        // Recursively process any descendant blocks that now may be eligible to be connected.
        while (!queue.empty()) {
            CBlockIndex* pindex = queue.front();
            queue.pop_front();
            pindex->nChainTx = (pindex->pprev ? pindex->pprev->nChainTx : 0) + pindex->nTx;
            {
                LOCK(cs_nBlockSequenceId);
                pindex->nSequenceId = nBlockSequenceId++;
            }
            if (chain.Tip() == NULL || !blockIndexCandidates.value_comp()(pindex, chain.Tip())) {
                blockIndexCandidates.insert(pindex);
            }
            auto range = blockIndexSuccessorsByPrevBlockIndex.equal_range(pindex);
            while (range.first != range.second) {
                auto it = range.first;
                queue.push_back(it->second);
                range.first++;
                blockIndexSuccessorsByPrevBlockIndex.erase(it);
            }
        }
    } else {
        if (newlyConnectedBlockIndex->pprev && newlyConnectedBlockIndex->pprev->IsValid(BLOCK_VALID_TREE)) {
            blockIndexSuccessorsByPrevBlockIndex.insert(std::make_pair(newlyConnectedBlockIndex->pprev, newlyConnectedBlockIndex));
        }
    }
}

/** Delete all entries in setBlockIndexCandidates that are worse than the current tip. */
void PruneBlockIndexCandidates(const CChain& chain)
{
    std::set<CBlockIndex*, CBlockIndexWorkComparator>& candidateBlockIndices = GetBlockIndexCandidates();
    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    auto it = candidateBlockIndices.begin();
    while (it != candidateBlockIndices.end() && candidateBlockIndices.value_comp()(*it, chain.Tip())) {
        candidateBlockIndices.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left in setBlockIndexCandidates.
    assert(!candidateBlockIndices.empty());
}

void InvalidChainFound(
    const bool isInitialBlockDownload,
    const Settings& settings,
    CCriticalSection& mainCriticalSection,
    CBlockIndex* pindexNew)
{
    updateMostWorkInvalidBlockIndex(pindexNew);

    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    LogPrintf("InvalidChainFound: invalid block=%s  height=%d  log2_work=%.8g  date=%s\n",
              pindexNew->GetBlockHash(), pindexNew->nHeight,
              log(pindexNew->nChainWork.getdouble()) / log(2.0), DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
                                                                                   pindexNew->GetBlockTime()));
    LogPrintf("InvalidChainFound:  current best=%s  height=%d  log2_work=%.8g  date=%s\n",
              chain.Tip()->GetBlockHash(), chain.Height(), log(chain.Tip()->nChainWork.getdouble()) / log(2.0),
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chain.Tip()->GetBlockTime()));
    CheckForkWarningConditions(settings, mainCriticalSection, isInitialBlockDownload);
}

bool InvalidateBlock(
    const I_ChainTipManager& chainTipManager,
    const bool isInitialBlockDownload,
    const Settings& settings,
    CValidationState& state,
    CCriticalSection& mainCriticalSection,
    ChainstateManager& chainstate,
    CBlockIndex* pindex,
    const bool updateCoinDatabaseOnly)
{
    AssertLockHeld(mainCriticalSection);

    const auto& chain = chainstate.ActiveChain();
    auto& blockMap = chainstate.GetBlockMap();

    // Mark the block itself as invalid.
    pindex->nStatus |= BLOCK_FAILED_VALID;
    BlockFileHelpers::RecordDirtyBlockIndex(pindex);
    std::set<CBlockIndex*, CBlockIndexWorkComparator>& candidateBlockIndices = GetBlockIndexCandidates();
    candidateBlockIndices.erase(pindex);

    while (chain.Contains(pindex)) {
        CBlockIndex* pindexWalk = blockMap.at(chain.Tip()->GetBlockHash());
        pindexWalk->nStatus |= BLOCK_FAILED_CHILD;
        BlockFileHelpers::RecordDirtyBlockIndex(pindexWalk);
        candidateBlockIndices.erase(pindexWalk);
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!chainTipManager.disconnectTip(state, updateCoinDatabaseOnly)) {
            return false;
        }
    }

    // The resulting new best tip may not be in candidateBlockIndices anymore, so
    // add them again.
    for (const auto& entry : blockMap) {
        if (entry.second->IsValid(BLOCK_VALID_TRANSACTIONS) && entry.second->nChainTx && !candidateBlockIndices.value_comp()(entry.second, chain.Tip())) {
            candidateBlockIndices.insert(entry.second);
        }
    }

    InvalidChainFound(isInitialBlockDownload, settings, mainCriticalSection, pindex);
    return true;
}


//! List of asynchronously-determined block rejections to notify this peer about.
void InvalidBlockFound(
    const std::map<uint256, NodeId>& peerIdByBlockHash,
    const bool isInitialBlockDownload,
    const Settings& settings,
    CCriticalSection& mainCriticalSection,
    CBlockIndex* pindex,
    const CValidationState& state)
{
    int nDoS = 0;
    if (state.IsInvalid(nDoS)) {
        std::map<uint256, NodeId>::const_iterator it = peerIdByBlockHash.find(pindex->GetBlockHash());
        if (it != peerIdByBlockHash.end()) {
            Misbehaving(it->second,nDoS,"Invalid block sourced from peer");
        }
    }
    if (!state.CorruptionPossible()) {
        std::set<CBlockIndex*, CBlockIndexWorkComparator>& candidateBlockIndices = GetBlockIndexCandidates();
        pindex->nStatus |= BLOCK_FAILED_VALID;
        BlockFileHelpers::RecordDirtyBlockIndex(pindex);
        candidateBlockIndices.erase(pindex);
        InvalidChainFound(isInitialBlockDownload, settings, mainCriticalSection, pindex);
    }
}
