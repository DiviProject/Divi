#include <BlocksInFlightRegistry.h>
#include <uint256.h>
#include <chain.h>
#include <utiltime.h>
#include <Logging.h>

BlocksInFlightRegistry::BlocksInFlightRegistry(
    ): blocksInFlightByNodeId_()
    , stallingStartTimestampByNodeId_()
    , blocksInFlight_()
    , queuedValidatedHeadersCount_(0)
{

}

std::list<QueuedBlock>& BlocksInFlightRegistry::RegisterNodedId(NodeId nodeId)
{
    stallingStartTimestampByNodeId_[nodeId] = 0;
    return blocksInFlightByNodeId_[nodeId];
}
void BlocksInFlightRegistry::UnregisterNodeId(NodeId nodeId)
{
    if(blocksInFlightByNodeId_.count(nodeId)==0) return;
    for(const QueuedBlock& entry: blocksInFlightByNodeId_[nodeId])
        DiscardBlockInFlight(entry.hash);
    blocksInFlightByNodeId_.erase(nodeId);
    stallingStartTimestampByNodeId_.erase(nodeId);
}
// Requires cs_main.
void BlocksInFlightRegistry::MarkBlockAsReceived(const uint256& hash)
{
    std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> >::iterator itInFlight = blocksInFlight_.find(hash);
    if (itInFlight != blocksInFlight_.end()) {
        NodeId nodeId = itInFlight->second.first;
        queuedValidatedHeadersCount_ -= itInFlight->second.second->fValidatedHeaders;
        blocksInFlightByNodeId_[nodeId].erase(itInFlight->second.second);
        stallingStartTimestampByNodeId_[nodeId] = 0;
        blocksInFlight_.erase(itInFlight);
    }
}

// Requires cs_main.
void BlocksInFlightRegistry::MarkBlockAsInFlight(NodeId nodeId, const uint256& hash, CBlockIndex* pindex)
{
    // Make sure it's not listed somewhere already.
    MarkBlockAsReceived(hash);

    QueuedBlock newentry = {hash, pindex, GetTimeMicros(), queuedValidatedHeadersCount_, pindex != NULL};
    queuedValidatedHeadersCount_ += newentry.fValidatedHeaders;
    std::list<QueuedBlock>& blocksInFlight = blocksInFlightByNodeId_[nodeId];
    std::list<QueuedBlock>::iterator it = blocksInFlight.insert(blocksInFlight.end(), newentry);
    blocksInFlight_[hash] = std::make_pair(nodeId, it);
}
// Requires cs_main.
void BlocksInFlightRegistry::DiscardBlockInFlight(const uint256& hash)
{
    blocksInFlight_.erase(hash);
}
bool BlocksInFlightRegistry::BlockIsInFlight(const uint256& hash)
{
    return blocksInFlight_.count(hash)> 0;
}
NodeId BlocksInFlightRegistry::GetSourceOfInFlightBlock(const uint256& hash)
{
    return blocksInFlight_[hash].first;
}

bool BlocksInFlightRegistry::BlockDownloadHasTimedOut(NodeId nodeId, int64_t nNow, int64_t targetSpacing) const
{
    auto it = blocksInFlightByNodeId_.find(nodeId);
    if(it != blocksInFlightByNodeId_.end())
    {
        const std::list<QueuedBlock>& vBlocksInFlight = it->second;
        const int64_t maxTimeout = nNow - 500000 * targetSpacing * (4 + vBlocksInFlight.front().nValidatedQueuedBefore);
        const bool timedOut = vBlocksInFlight.size() > 0 &&
            vBlocksInFlight.front().nTime < maxTimeout;
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
    if(stallingStartTimestampByNodeId_.count(nodeId)==0) return false;
    const int64_t& nStallingSince = stallingStartTimestampByNodeId_.find(nodeId)->second;
    return nStallingSince > 0 && nStallingSince < nNow - stallingWindow;
}

void BlocksInFlightRegistry::RecordWhenStallingBegan(NodeId nodeId, int64_t currentTimestamp)
{
    if(stallingStartTimestampByNodeId_.count(nodeId)==0) return;
    int64_t& nStallingSince = stallingStartTimestampByNodeId_[nodeId];
    if(nStallingSince==0)
    {
        nStallingSince = currentTimestamp;
        LogPrint("net", "Stall started peer=%d\n", nodeId);
    }
}

std::vector<int> BlocksInFlightRegistry::GetBlockHeightsInFlight(NodeId nodeId) const
{
    if(blocksInFlightByNodeId_.count(nodeId)==0) return {};
    std::vector<int> blockHeights;
    const std::list<QueuedBlock>& blocksInFlight = blocksInFlightByNodeId_.find(nodeId)->second;
    blockHeights.reserve(blocksInFlight.size());
    for(const QueuedBlock& queue: blocksInFlight) {
        if (queue.pindex)
            blockHeights.push_back(queue.pindex->nHeight);
    }
    return blockHeights;
}
int BlocksInFlightRegistry::GetNumberOfBlocksInFlight(NodeId nodeId) const
{
    if(blocksInFlightByNodeId_.count(nodeId)==0) return 0;
    return blocksInFlightByNodeId_.find(nodeId)->second.size();
}