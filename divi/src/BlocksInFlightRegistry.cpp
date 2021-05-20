#include <BlocksInFlightRegistry.h>
#include <uint256.h>
#include <chain.h>
#include <NodeState.h>
#include <utiltime.h>

BlocksInFlightRegistry::BlocksInFlightRegistry(
    ): blocksInFlightByNodeId_()
    , blocksInFlight_()
    , queuedValidatedHeadersCount_(0)
{

}

std::list<QueuedBlock>& BlocksInFlightRegistry::RegisterNodedId(NodeId nodeId)
{
    return blocksInFlightByNodeId_[nodeId];
}
void BlocksInFlightRegistry::UnregisterNodeId(NodeId nodeId)
{
    if(blocksInFlightByNodeId_.count(nodeId)==0) return;
    for(const QueuedBlock& entry: blocksInFlightByNodeId_[nodeId])
        DiscardBlockInFlight(entry.hash);
    blocksInFlightByNodeId_.erase(nodeId);
}
// Requires cs_main.
void BlocksInFlightRegistry::MarkBlockAsReceived(const uint256& hash)
{
    std::map<uint256, std::pair<CNodeState*, std::list<QueuedBlock>::iterator> >::iterator itInFlight = blocksInFlight_.find(hash);
    if (itInFlight != blocksInFlight_.end()) {
        CNodeState* state = itInFlight->second.first;
        queuedValidatedHeadersCount_ -= itInFlight->second.second->fValidatedHeaders;
        blocksInFlightByNodeId_[state->nodeId].erase(itInFlight->second.second);
        state->nBlocksInFlight--;
        state->nStallingSince = 0;
        blocksInFlight_.erase(itInFlight);
    }
}

// Requires cs_main.
void BlocksInFlightRegistry::MarkBlockAsInFlight(CNodeState* nodeState, const uint256& hash, CBlockIndex* pindex)
{
    assert(nodeState != NULL);
    // Make sure it's not listed somewhere already.
    MarkBlockAsReceived(hash);

    QueuedBlock newentry = {hash, pindex, GetTimeMicros(), queuedValidatedHeadersCount_, pindex != NULL};
    queuedValidatedHeadersCount_ += newentry.fValidatedHeaders;
    std::list<QueuedBlock>& blocksInFlight = blocksInFlightByNodeId_[nodeState->nodeId];
    std::list<QueuedBlock>::iterator it = blocksInFlight.insert(blocksInFlight.end(), newentry);
    nodeState->nBlocksInFlight++;
    blocksInFlight_[hash] = std::make_pair(nodeState, it);
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
    return blocksInFlight_[hash].first->nodeId;
}