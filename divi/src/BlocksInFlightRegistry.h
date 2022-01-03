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

class NodeBlockSync
{
public:
    std::list<QueuedBlock> blocksInFlight;
    int64_t stallingTimestamp;
    NodeBlockSync(): blocksInFlight(), stallingTimestamp(0)
    {
    }
    void RecordReceivedBlock(std::list<QueuedBlock>::iterator it)
    {
        blocksInFlight.erase(it);
        stallingTimestamp = 0;
    }
    bool RecordSyncStalling(int64_t currentTimestamp)
    {
        if(stallingTimestamp==0)
        {
            stallingTimestamp = currentTimestamp;
            return true;
        }
        return false;
    }
};
class BlocksInFlightRegistry
{
private:
    std::map<NodeId, NodeBlockSync> nodeSyncByNodeId_;
    std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> > allBlocksInFlight_;
    int queuedValidatedHeadersCount_;
public:
    BlocksInFlightRegistry();
    void RegisterNodedId(NodeId nodeId);
    void UnregisterNodeId(NodeId nodeId);
    void MarkBlockAsReceived(const uint256& hash);
    void MarkBlockAsInFlight(NodeId nodeId, const uint256& hash, const CBlockIndex* pindex = nullptr);
    bool BlockIsInFlight(const uint256& hash);
    NodeId GetSourceOfInFlightBlock(const uint256& hash);

    bool BlockDownloadHasTimedOut(NodeId nodeId, int64_t nNow, int64_t targetSpacing) const;
    bool BlockDownloadHasStalled(NodeId nodeId, int64_t nNow, int64_t stallingWindow) const;
    void RecordWhenStallingBegan(NodeId nodeId, int64_t currentTimestamp);
    std::vector<int> GetBlockHeightsInFlight(NodeId nodeId) const;
    int GetNumberOfBlocksInFlight(NodeId nodeId) const;
};
#endif// BLOCKS_IN_FLIGHT_REGISTRY_H
