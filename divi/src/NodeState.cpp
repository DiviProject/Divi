#include <NodeState.h>

#include <BlocksInFlightRegistry.h>
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
    BlocksInFlightRegistry& blocksInFlightRegistry,
    CAddrMan& addressManager
    ): blocksInFlightRegistry_(blocksInFlightRegistry)
    , addressManager_(addressManager)
    , nMisbehavior(0)
    , vBlocksInFlight(blocksInFlightRegistry_.RegisterNodedId(nodeIdValue))
    , nStallingSince(0)
    , nodeId(nodeIdValue)
    , fCurrentlyConnected(false)
    , fShouldBan(false)
    , pindexBestKnownBlock(nullptr)
    , hashLastUnknownBlock(uint256(0))
    , pindexLastCommonBlock(nullptr)
    , fSyncStarted(false)
    , nBlocksInFlight(0)
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
    blocksInFlightRegistry_.UnregisterNodeId(nodeId);
    EraseOrphansFor(nodeId);
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

bool CNodeState::BlockDownloadTimedOut(int64_t nNow, int64_t targetSpacing) const
{
    const int64_t maxTimeout = nNow - 500000 * targetSpacing * (4 + vBlocksInFlight.front().nValidatedQueuedBefore);
    const bool timedOut = vBlocksInFlight.size() > 0 &&
        vBlocksInFlight.front().nTime < maxTimeout;
    if(timedOut)
    {
        LogPrintf("Timeout downloading block %s from peer=%d, disconnecting\n", vBlocksInFlight.front().hash, nodeId);
    }
    return timedOut;
}
bool CNodeState::BlockDownloadIsStalling(int64_t nNow, int64_t stallingWindow) const
{
    return nStallingSince > 0 && nStallingSince < nNow - stallingWindow;
}
void CNodeState::RecordWhenStallingBegan(int64_t currentTimestamp)
{
    if (nStallingSince == 0) {
        nStallingSince = currentTimestamp;
        LogPrint("net", "Stall started peer=%d\n", nodeId);
    }
}
void CNodeState::ResetStallingTimestamp()
{
    nStallingSince = 0;
}
std::vector<int> CNodeState::GetBlockHeightsInFlight() const
{
    std::vector<int> blockHeights;
    blockHeights.reserve(vBlocksInFlight.size());
    for(const QueuedBlock& queue: vBlocksInFlight) {
        if (queue.pindex)
            blockHeights.push_back(queue.pindex->nHeight);
    }
    return blockHeights;
}