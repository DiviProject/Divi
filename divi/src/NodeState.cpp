#include <NodeState.h>

#include <BlocksInFlightRegistry.h>

/** Number of nodes with fSyncStarted. */
int CNodeState::countOfNodesAlreadySyncing = 0;
/** Number of preferable block download peers. */
int CNodeState::numberOfPreferredDownloadSources = 0;

CNodeState::CNodeState(
    NodeId nodeIdValue,
    BlocksInFlightRegistry& blocksInFlightRegistry
    ): blocksInFlightRegistry_(blocksInFlightRegistry)
    , nodeId(nodeIdValue)
    , fCurrentlyConnected(false)
    , nMisbehavior(0)
    , fShouldBan(false)
    , pindexBestKnownBlock(nullptr)
    , hashLastUnknownBlock(uint256(0))
    , pindexLastCommonBlock(nullptr)
    , fSyncStarted(false)
    , nStallingSince(0)
    , nBlocksInFlight(0)
    , fPreferredDownload(false)
{
}

CNodeState::~CNodeState()
{
    if(fSyncStarted) --countOfNodesAlreadySyncing;
    if(fPreferredDownload) --numberOfPreferredDownloadSources;
    for(const QueuedBlock& entry: vBlocksInFlight)
        blocksInFlightRegistry_.DiscardBlockInFlight(entry.hash);
}

void CNodeState::RecordNodeStartedToSync()
{
    fSyncStarted = true;
    ++countOfNodesAlreadySyncing;
}
void CNodeState::UpdatePreferredDownload(bool updatedStatus)
{
    if(fPreferredDownload != updatedStatus)
    {
        numberOfPreferredDownloadSources += (updatedStatus)? 1:-1;
    }
    fPreferredDownload = updatedStatus;
}
bool CNodeState::NodeSyncStarted()
{
    return countOfNodesAlreadySyncing > 0;
}
bool CNodeState::HavePreferredDownloadPeers()
{
    return numberOfPreferredDownloadSources > 0;
}
// Requires cs_main.
void CNodeState::MarkBlockAsInFlight(const uint256& hash, CBlockIndex* pindex)
{
    blocksInFlightRegistry_.MarkBlockAsInFlight(this,hash,pindex);
}