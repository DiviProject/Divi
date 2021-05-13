#include <NodeStateRegistry.h>

#include <NodeState.h>
#include <sync.h>
#include <NodeId.h>
#include <net.h>
#include <OrphanTransactions.h>
#include <uint256.h>
#include <chain.h>
#include <utiltime.h>
#include <blockmap.h>

extern CCriticalSection cs_main;

/** Number of blocks in flight with validated headers. */
int nQueuedValidatedHeaders = 0;
std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> > mapBlocksInFlight;

/** Number of preferable block download peers. */
int nPreferredDownload = 0;
/** Number of nodes with fSyncStarted. */
int nSyncStarted = 0;
/** Map maintaining per-node state. Requires cs_main. */
std::map<NodeId, CNodeState> mapNodeState;
// Requires cs_main.
CNodeState* State(NodeId nodeId)
{
    std::map<NodeId, CNodeState>::iterator it = mapNodeState.find(nodeId);
    if (it == mapNodeState.end())
        return NULL;
    return &it->second;
}

void InitializeNode(NodeId nodeid, const CNode* pnode)
{
    LOCK(cs_main);
    CNodeState& state = mapNodeState.insert(std::make_pair(nodeid, CNodeState())).first->second;
    state.name = pnode->addrName;
    state.address = pnode->addr;
}

void FinalizeNode(NodeId nodeid)
{
    LOCK(cs_main);
    CNodeState* state = State(nodeid);

    if (state->fSyncStarted)
        nSyncStarted--;

    if (state->nMisbehavior == 0 && state->fCurrentlyConnected) {
        RecordAddressAsCurrentlyConnected(state->address);
    }

    for(const QueuedBlock& entry: state->vBlocksInFlight)
            mapBlocksInFlight.erase(entry.hash);
    EraseOrphansFor(nodeid);
    nPreferredDownload -= state->fPreferredDownload;

    mapNodeState.erase(nodeid);
}

void UpdatePreferredDownload(CNode* node, CNodeState* state)
{
    nPreferredDownload -= state->fPreferredDownload;

    // Whether this node should be marked as a preferred download node.
    state->fPreferredDownload = (!node->fInbound || node->fWhitelisted) && !node->fOneShot && !node->fClient;

    nPreferredDownload += state->fPreferredDownload;
}

bool HavePreferredDownloadPeers()
{
    return nPreferredDownload > 0;
}
int SyncStartedNodeCount()
{
    return nSyncStarted;
}
void RecordNodeStartedToSync()
{
    ++nSyncStarted;
}

// Requires cs_main.
void MarkBlockAsReceived(const uint256& hash)
{
    std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> >::iterator itInFlight = mapBlocksInFlight.find(hash);
    if (itInFlight != mapBlocksInFlight.end()) {
        CNodeState* state = State(itInFlight->second.first);
        nQueuedValidatedHeaders -= itInFlight->second.second->fValidatedHeaders;
        state->vBlocksInFlight.erase(itInFlight->second.second);
        state->nBlocksInFlight--;
        state->nStallingSince = 0;
        mapBlocksInFlight.erase(itInFlight);
    }
}

// Requires cs_main.
void MarkBlockAsInFlight(NodeId nodeid, const uint256& hash, CBlockIndex* pindex)
{
    CNodeState* state = State(nodeid);
    assert(state != NULL);

    // Make sure it's not listed somewhere already.
    MarkBlockAsReceived(hash);

    QueuedBlock newentry = {hash, pindex, GetTimeMicros(), nQueuedValidatedHeaders, pindex != NULL};
    nQueuedValidatedHeaders += newentry.fValidatedHeaders;
    std::list<QueuedBlock>::iterator it = state->vBlocksInFlight.insert(state->vBlocksInFlight.end(), newentry);
    state->nBlocksInFlight++;
    mapBlocksInFlight[hash] = std::make_pair(nodeid, it);
}
bool BlockIsInFlight(const uint256& hash)
{
    return mapBlocksInFlight.count(hash)> 0;
}
NodeId GetSourceOfInFlightBlock(const uint256& hash)
{
    return mapBlocksInFlight[hash].first;
}
/** Check whether the last unknown block a peer advertized is not yet known. */
void ProcessBlockAvailability(const BlockMap& blockIndicesByHash, NodeId nodeid)
{
    CNodeState* state = State(nodeid);
    assert(state != NULL);

    if (state->hashLastUnknownBlock != 0) {
        BlockMap::const_iterator itOld = blockIndicesByHash.find(state->hashLastUnknownBlock);
        if (itOld != blockIndicesByHash.end() && itOld->second->nChainWork > 0) {
            if (state->pindexBestKnownBlock == NULL || itOld->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
                state->pindexBestKnownBlock = itOld->second;
            state->hashLastUnknownBlock = uint256(0);
        }
    }
}