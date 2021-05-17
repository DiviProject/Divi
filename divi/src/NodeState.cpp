#include <NodeState.h>

/** Number of nodes with fSyncStarted. */
int CNodeState::countOfNodesAlreadySyncing = 0;
/** Number of preferable block download peers. */
int CNodeState::numberOfPreferredDownloadSources = 0;

CNodeState::CNodeState(
    NodeId nodeIdValue
    ): nodeId(nodeIdValue)
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