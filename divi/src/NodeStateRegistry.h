#ifndef NODE_STATE_REGISTRY_H
#define NODE_STATE_REGISTRY_H
#include <NodeId.h>
#include <vector>
#include <string>
#include <stdint.h>

class CChain;
struct CNodeState;
class uint256;
class CBlockIndex;
class BlockMap;
class CAddress;
class CAddrMan;
class CBlockReject;

// Requires cs_main.
CNodeState* State(NodeId nodeId);
void RecordInvalidBlockFromPeer(NodeId nodeId, const CBlockReject& blockReject, int nDoS);
void UpdateStateToCurrentlyConnected(NodeId nodeId);
void UpdateStallingTimestamp(NodeId nodeId, int64_t currentTimestamp);
CAddrMan& GetNetworkAddressManager();
void InitializeNode(NodeId nodeid, const std::string addressName, const CAddress& addr);
void FinalizeNode(NodeId nodeid);
void UpdatePreferredDownload(NodeId nodeId, bool updatedStatus);
void MarkBlockAsReceived(const uint256& hash);
void MarkBlockAsInFlight(NodeId nodeid, const uint256& hash, CBlockIndex* pindex = nullptr);
bool BlockIsInFlight(const uint256& hash);
NodeId GetSourceOfInFlightBlock(const uint256& hash);
void ProcessBlockAvailability(const BlockMap& blockIndicesByHash, NodeId nodeid);
void UpdateBlockAvailability(const BlockMap& blockIndicesByHash, NodeId nodeid, const uint256& hash);
void FindNextBlocksToDownload(
    const BlockMap& blockIndicesByHash,
    const CChain& activeChain,
    NodeId nodeid,
    unsigned int count,
    std::vector<CBlockIndex*>& vBlocks,
    NodeId& nodeStaller);
// Requires cs_main.
/** Increase a node's misbehavior score. */
bool Misbehaving(NodeId nodeId, int howmuch);
#endif// NODE_STATE_REGISTRY_H