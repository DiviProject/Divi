#ifndef BLOCKS_IN_FLIGHT_REGISTRY_H
#define BLOCKS_IN_FLIGHT_REGISTRY_H
#include <map>
#include <list>
#include <utility>
#include <QueuedBlock.h>
#include <NodeId.h>
struct CNodeState;
class uint256;
class CBlockIndex;

class BlocksInFlightRegistry
{
private:
    std::map<NodeId, std::list<QueuedBlock>> blocksInFlightByNodeId_;
    std::map<NodeId, int64_t> stallingStartTimestampByNodeId_;
    std::map<uint256, std::pair<CNodeState*, std::list<QueuedBlock>::iterator> > blocksInFlight_;
    int queuedValidatedHeadersCount_;
public:
    BlocksInFlightRegistry();
    std::list<QueuedBlock>& RegisterNodedId(NodeId nodeId);
    void UnregisterNodeId(NodeId nodeId);
    void MarkBlockAsReceived(const uint256& hash);
    void MarkBlockAsInFlight(CNodeState* nodeState, const uint256& hash, CBlockIndex* pindex = nullptr);
    void DiscardBlockInFlight(const uint256& hash);
    bool BlockIsInFlight(const uint256& hash);
    NodeId GetSourceOfInFlightBlock(const uint256& hash);

    bool BlockDownloadHasTimedOut(NodeId nodeId, int64_t nNow, int64_t targetSpacing) const;
    bool BlockDownloadHasStalled(NodeId nodeId, int64_t nNow, int64_t stallingWindow) const;
    void RecordWhenStallingBegan(NodeId nodeId, int64_t currentTimestamp);
    std::vector<int> GetBlockHeightsInFlight(NodeId nodeId) const;
    int GetNumberOfBlocksInFlight(NodeId nodeId) const;
};
#endif// BLOCKS_IN_FLIGHT_REGISTRY_H