#include <BlocksInFlightRegistry.h>
#include <uint256.h>
#include <chain.h>
#include <utiltime.h>
#include <Logging.h>

BlocksInFlightRegistry::BlocksInFlightRegistry(
    ): nodeSyncByNodeId_()
    , allBlocksInFlight_()
    , queuedValidatedHeadersCount_(0)
{
}

void BlocksInFlightRegistry::RegisterNodedId(NodeId nodeId)
{
    if(nodeSyncByNodeId_.count(nodeId)>0) return;
    nodeSyncByNodeId_[nodeId] = NodeBlockSync();
}
void BlocksInFlightRegistry::UnregisterNodeId(NodeId nodeId)
{
    if(nodeSyncByNodeId_.count(nodeId)==0) return;
    for(const QueuedBlock& entry: nodeSyncByNodeId_[nodeId].blocksInFlight)
        allBlocksInFlight_.erase(entry.hash);
    nodeSyncByNodeId_.erase(nodeId);
}
// Requires cs_main.
void BlocksInFlightRegistry::MarkBlockAsReceived(const uint256& hash)
{
    std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> >::iterator itInFlight = allBlocksInFlight_.find(hash);
    if (itInFlight != allBlocksInFlight_.end()) {
        NodeId nodeId = itInFlight->second.first;
        queuedValidatedHeadersCount_ -= itInFlight->second.second->fValidatedHeaders;
        nodeSyncByNodeId_[nodeId].RecordReceivedBlock(itInFlight->second.second);
        allBlocksInFlight_.erase(itInFlight);
    }
}

// Requires cs_main.
void BlocksInFlightRegistry::MarkBlockAsInFlight(NodeId nodeId, const uint256& hash, const CBlockIndex* pindex)
{
    assert(nodeSyncByNodeId_.count(nodeId)>0);
    // Make sure it's not listed somewhere already.
    MarkBlockAsReceived(hash);

    QueuedBlock newentry = {hash, pindex, GetTimeMicros(), queuedValidatedHeadersCount_, pindex != NULL};
    queuedValidatedHeadersCount_ += newentry.fValidatedHeaders;
    std::list<QueuedBlock>& blocksInFlight = nodeSyncByNodeId_[nodeId].blocksInFlight;
    std::list<QueuedBlock>::iterator it = blocksInFlight.insert(blocksInFlight.end(), newentry);
    allBlocksInFlight_[hash] = std::make_pair(nodeId, it);
}
// Requires cs_main.
bool BlocksInFlightRegistry::BlockIsInFlight(const uint256& hash)
{
    return allBlocksInFlight_.count(hash)> 0;
}
NodeId BlocksInFlightRegistry::GetSourceOfInFlightBlock(const uint256& hash)
{
    return allBlocksInFlight_[hash].first;
}

bool BlocksInFlightRegistry::BlockDownloadHasTimedOut(NodeId nodeId, int64_t nNow, int64_t targetSpacing) const
{
    auto it = nodeSyncByNodeId_.find(nodeId);
    if(it != nodeSyncByNodeId_.end())
    {
        const std::list<QueuedBlock>& vBlocksInFlight = it->second.blocksInFlight;
        if(vBlocksInFlight.size() < 1u) return false;
        const int64_t maxTimeout = nNow - 500000 * targetSpacing * (4 + vBlocksInFlight.front().nValidatedQueuedBefore);
        const bool timedOut = vBlocksInFlight.front().nTime < maxTimeout;
        if(timedOut)
        {
            LogPrintf("Timeout downloading block %s from peer=%d, disconnecting\n", vBlocksInFlight.front().hash, nodeId);
        }
        return timedOut;
    }
    return false;
}
bool BlocksInFlightRegistry::BlockDownloadHasStalled(NodeId nodeId, int64_t nNow, int64_t stallingWindow) const
{
    if(nodeSyncByNodeId_.count(nodeId)==0) return false;
    const int64_t& nStallingSince = nodeSyncByNodeId_.find(nodeId)->second.stallingTimestamp;
    return nStallingSince > 0 && nStallingSince < nNow - stallingWindow;
}

void BlocksInFlightRegistry::RecordWhenStallingBegan(NodeId nodeId, int64_t currentTimestamp)
{
    if(nodeSyncByNodeId_.count(nodeId)==0) return;
    if(nodeSyncByNodeId_[nodeId].RecordSyncStalling(currentTimestamp))
    {
        LogPrint("net", "Stall started peer=%d\n", nodeId);
    }
}

std::vector<int> BlocksInFlightRegistry::GetBlockHeightsInFlight(NodeId nodeId) const
{
    if(nodeSyncByNodeId_.count(nodeId)==0) return {};
    std::vector<int> blockHeights;
    const std::list<QueuedBlock>& blocksInFlight = nodeSyncByNodeId_.find(nodeId)->second.blocksInFlight;
    blockHeights.reserve(blocksInFlight.size());
    for(const QueuedBlock& queue: blocksInFlight) {
        if (queue.pindex)
            blockHeights.push_back(queue.pindex->nHeight);
    }
    return blockHeights;
}
int BlocksInFlightRegistry::GetNumberOfBlocksInFlight(NodeId nodeId) const
{
    if(nodeSyncByNodeId_.count(nodeId)==0) return 0;
    return nodeSyncByNodeId_.find(nodeId)->second.blocksInFlight.size();
}
