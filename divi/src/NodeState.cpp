#include <NodeState.h>

/** Number of nodes with fSyncStarted. */
int CNodeState::countOfNodesAlreadySyncing = 0;

CNodeState::CNodeState(
    ): fCurrentlyConnected(false)
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
bool CNodeState::NodeSyncStarted()
{
    return countOfNodesAlreadySyncing > 0;
}