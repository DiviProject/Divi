#include <NodeStateRegistry.h>

#include <NodeState.h>
#include <sync.h>
#include <NodeId.h>
#include <OrphanTransactions.h>
#include <uint256.h>
#include <chain.h>
#include <utiltime.h>
#include <blockmap.h>
#include <Settings.h>
#include <BlocksInFlightRegistry.h>

extern Settings& settings;
extern CCriticalSection cs_main;

/** Number of blocks in flight with validated headers. */
BlocksInFlightRegistry blocksInFlightRegistry;

/** Map maintaining per-node state. Requires cs_main. */
std::map<NodeId, CNodeState*> mapNodeState;

// Requires cs_main.
CNodeState* State(NodeId nodeId)
{
    AssertLockHeld(cs_main);
    std::map<NodeId, CNodeState*>::iterator it = mapNodeState.find(nodeId);
    if (it == mapNodeState.end())
        return NULL;
    return it->second;
}
void InitializeNode(CNodeState& nodeState)
{
    LOCK(cs_main);
    CNodeState* state = mapNodeState.insert(std::make_pair(nodeState.nodeId, &nodeState)).first->second;
    state->SetBanScoreThreshold(settings.GetArg("-banscore", 100));
    blocksInFlightRegistry.RegisterNodedId(state->nodeId);
}

void FinalizeNode(NodeId nodeId)
{
    LOCK(cs_main);
    blocksInFlightRegistry.UnregisterNodeId(nodeId);
    mapNodeState.erase(nodeId);
}

bool Misbehaving(CNodeState* state, int howmuch, std::string cause)
{
    if (state == NULL)
        return false;

    if (howmuch == 0)
        return true;

    state->ApplyMisbehavingPenalty(howmuch,cause);
    return true;
}

bool Misbehaving(NodeId nodeId, int howmuch, std::string cause)
{
    AssertLockHeld(cs_main);
    CNodeState* state = State(nodeId);
    return Misbehaving(state,howmuch,cause);
}

// Requires cs_main.
void MarkBlockAsReceived(const uint256& hash)
{
    AssertLockHeld(cs_main);
    blocksInFlightRegistry.MarkBlockAsReceived(hash);
}

// Requires cs_main.
void MarkBlockAsInFlight(NodeId nodeid, const uint256& hash, const CBlockIndex* pindex)
{
    AssertLockHeld(cs_main);
    blocksInFlightRegistry.MarkBlockAsInFlight(nodeid,hash,pindex);
}
bool BlockIsInFlight(const uint256& hash)
{
    AssertLockHeld(cs_main);
    return blocksInFlightRegistry.BlockIsInFlight(hash);
}
bool BlockDownloadHasTimedOut(NodeId nodeId, int64_t nNow, int64_t targetSpacing)
{
    AssertLockHeld(cs_main);
    return blocksInFlightRegistry.BlockDownloadHasTimedOut(nodeId,nNow,targetSpacing);
}
bool BlockDownloadHasStalled(NodeId nodeId, int64_t nNow, int64_t stallingWindow)
{
    AssertLockHeld(cs_main);
    return blocksInFlightRegistry.BlockDownloadHasStalled(nodeId,nNow,stallingWindow);
}
void RecordWhenStallingBegan(NodeId nodeId, int64_t currentTimestamp)
{
    AssertLockHeld(cs_main);
    blocksInFlightRegistry.RecordWhenStallingBegan(nodeId,currentTimestamp);
}
std::vector<int> GetBlockHeightsInFlight(NodeId nodeId)
{
    AssertLockHeld(cs_main);
    return blocksInFlightRegistry.GetBlockHeightsInFlight(nodeId);
}
int GetNumberOfBlocksInFlight(NodeId nodeId)
{
    AssertLockHeld(cs_main);
    return blocksInFlightRegistry.GetNumberOfBlocksInFlight(nodeId);
}
NodeId GetSourceOfInFlightBlock(const uint256& blockhash)
{
    AssertLockHeld(cs_main);
    return blocksInFlightRegistry.GetSourceOfInFlightBlock(blockhash);
}

/** Check whether the last unknown block a peer advertized is not yet known. */
void ProcessBlockAvailability(const BlockMap& blockIndicesByHash, CNodeState* state)
{
    if (state->hashLastUnknownBlock != 0) {
        BlockMap::const_iterator itOld = blockIndicesByHash.find(state->hashLastUnknownBlock);
        if (itOld != blockIndicesByHash.end() && itOld->second->nChainWork > 0) {
            if (state->pindexBestKnownBlock == NULL || itOld->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
                state->pindexBestKnownBlock = itOld->second;
            state->hashLastUnknownBlock = uint256(0);
        }
    }
}
/** Update tracking information about which blocks a peer is assumed to have. */
void UpdateBlockAvailability(const BlockMap& blockIndicesByHash, CNodeState* state, const uint256& hash)
{
    assert(state != NULL);
    ProcessBlockAvailability(blockIndicesByHash,state);

    BlockMap::const_iterator it = blockIndicesByHash.find(hash);
    if (it != blockIndicesByHash.end() && it->second->nChainWork > 0) {
        // An actually better block was announced.
        if (state->pindexBestKnownBlock == NULL || it->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
            state->pindexBestKnownBlock = it->second;
    } else {
        // An unknown block was announced; just assume that the latest one is the best one.
        state->hashLastUnknownBlock = hash;
    }
}

/** Update pindexLastCommonBlock and add not-in-flight missing successors to vBlocks, until it has
 *  at most count entries. */
void FindNextBlocksToDownload(
    const BlockMap& blockIndicesByHash,
    const CChain& activeChain,
    CNodeState* state,
    unsigned int count,
    std::vector<const CBlockIndex*>& vBlocks,
    NodeId& nodeStaller)
{
    if (count == 0)
        return;

    vBlocks.reserve(vBlocks.size() + count);
    assert(state != NULL);

    // Make sure pindexBestKnownBlock is up to date, we'll need it.
    ProcessBlockAvailability(blockIndicesByHash,state);

    if (state->pindexBestKnownBlock == NULL || state->pindexBestKnownBlock->nChainWork < activeChain.Tip()->nChainWork) {
        // This peer has nothing interesting.
        return;
    }

    if (state->pindexLastCommonBlock == NULL) {
        // Bootstrap quickly by guessing a parent of our best tip is the forking point.
        // Guessing wrong in either direction is not a problem.
        state->pindexLastCommonBlock = activeChain[std::min(state->pindexBestKnownBlock->nHeight, activeChain.Height())];
    }

    // If the peer reorganized, our previous pindexLastCommonBlock may not be an ancestor
    // of their current tip anymore. Go back enough to fix that.
    state->pindexLastCommonBlock = LastCommonAncestor(state->pindexLastCommonBlock, state->pindexBestKnownBlock);
    if (state->pindexLastCommonBlock == state->pindexBestKnownBlock)
        return;

    std::vector<const CBlockIndex*> vToFetch;
    const CBlockIndex* pindexWalk = state->pindexLastCommonBlock;
    // Never fetch further than the best block we know the peer has, or more than BLOCK_DOWNLOAD_WINDOW + 1 beyond the last
    // linked block we have in common with this peer. The +1 is so we can detect stalling, namely if we would be able to
    // download that next block if the window were 1 larger.
    int nWindowEnd = state->pindexLastCommonBlock->nHeight + BLOCK_DOWNLOAD_WINDOW;
    int nMaxHeight = std::min<int>(state->pindexBestKnownBlock->nHeight, nWindowEnd + 1);
    NodeId waitingfor = -1;
    AssertLockHeld(cs_main);
    while (pindexWalk->nHeight < nMaxHeight) {
        // Read up to 128 (or more, if more blocks than that are needed) successors of pindexWalk (towards
        // pindexBestKnownBlock) into vToFetch. We fetch 128, because CBlockIndex::GetAncestor may be as expensive
        // as iterating over ~100 CBlockIndex* entries anyway.
        int nToFetch = std::min(nMaxHeight - pindexWalk->nHeight, std::max<int>(count - vBlocks.size(), 128));
        vToFetch.resize(nToFetch);
        pindexWalk = state->pindexBestKnownBlock->GetAncestor(pindexWalk->nHeight + nToFetch);
        vToFetch[nToFetch - 1] = pindexWalk;
        for (unsigned int i = nToFetch - 1; i > 0; i--) {
            vToFetch[i - 1] = vToFetch[i]->pprev;
        }

        // Iterate over those blocks in vToFetch (in forward direction), adding the ones that
        // are not yet downloaded and not in flight to vBlocks. In the mean time, update
        // pindexLastCommonBlock as long as all ancestors are already downloaded.
        for(const auto* pindex: vToFetch) {
            if (!pindex->IsValid(BLOCK_VALID_TREE)) {
                // We consider the chain that this peer is on invalid.
                return;
            }
            if (pindex->nStatus & BLOCK_HAVE_DATA) {
                if (pindex->nChainTx)
                    state->pindexLastCommonBlock = pindex;
            } else if (!BlockIsInFlight(pindex->GetBlockHash()))
            {
                // The block is not already downloaded, and not yet in flight.
                if (pindex->nHeight > nWindowEnd)
                {
                    // We reached the end of the window.
                    if (vBlocks.size() == 0 && waitingfor != state->nodeId)
                    {
                        // We aren't able to fetch anything, but we would be if the download window was one larger.
                        nodeStaller = waitingfor;
                    }
                    return;
                }
                vBlocks.push_back(pindex);
                if (vBlocks.size() == count) {
                    return;
                }
            } else if (waitingfor == -1) {
                // This is the first already-in-flight block.
                waitingfor = GetSourceOfInFlightBlock(pindex->GetBlockHash());
            }
        }
    }
}
