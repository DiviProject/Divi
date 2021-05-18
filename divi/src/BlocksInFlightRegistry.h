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
    std::map<uint256, std::pair<CNodeState*, std::list<QueuedBlock>::iterator> > blocksInFlight_;
    int queuedValidatedHeadersCount_;
public:
    BlocksInFlightRegistry();
    void MarkBlockAsReceived(const uint256& hash);
    void MarkBlockAsInFlight(CNodeState* nodeState, const uint256& hash, CBlockIndex* pindex = nullptr);
    void DiscardBlockInFlight(const uint256& hash);
    bool BlockIsInFlight(const uint256& hash);
    NodeId GetSourceOfInFlightBlock(const uint256& hash);
};
#endif// BLOCKS_IN_FLIGHT_REGISTRY_H