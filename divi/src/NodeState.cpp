#include <NodeState.h>

#include <OrphanTransactions.h>
#include <addrman.h>
#include <Logging.h>
#include <chain.h>

/** Number of nodes with fSyncStarted. */
int CNodeState::countOfNodesAlreadySyncing = 0;
/** Number of preferable block download peers. */
int CNodeState::numberOfPreferredDownloadSources = 0;

CNodeState::CNodeState(
    NodeId nodeIdValue,
    CAddrMan& addressManager
    ): addressManager_(addressManager)
    , nMisbehavior(0)
    , fSyncStarted(false)
    , nodeId(nodeIdValue)
    , fCurrentlyConnected(false)
    , fShouldBan(false)
    , pindexBestKnownBlock(nullptr)
    , hashLastUnknownBlock(uint256(0))
    , pindexLastCommonBlock(nullptr)
    , fPreferredDownload(false)
{
}

CNodeState::~CNodeState()
{
}

void CNodeState::Finalize()
{
    if(fSyncStarted) --countOfNodesAlreadySyncing;
    if(fPreferredDownload) --numberOfPreferredDownloadSources;
    if (nMisbehavior == 0 && fCurrentlyConnected) {
        addressManager_.Connected(address);
    }
    EraseOrphansFor(nodeId);
}
bool CNodeState::Syncing() const
{
    return fSyncStarted;
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
void CNodeState::ApplyMisbehavingPenalty(int penaltyAmount, int banthreshold)
{
    int previousMisbehavior = nMisbehavior;
    nMisbehavior += penaltyAmount;
    if (nMisbehavior >= banthreshold && previousMisbehavior < banthreshold) {
        LogPrintf("Misbehaving: %s (%d -> %d) BAN THRESHOLD EXCEEDED\n", name, previousMisbehavior, nMisbehavior);
        fShouldBan = true;
    } else
        LogPrintf("Misbehaving: %s (%d -> %d)\n", name, previousMisbehavior, nMisbehavior);
}
int CNodeState::GetMisbehaviourPenalty() const
{
    return nMisbehavior;
}