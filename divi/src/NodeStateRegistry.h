#ifndef NODE_STATE_REGISTRY_H
#define NODE_STATE_REGISTRY_H
#include <NodeId.h>
#include <vector>
#include <string>

class CChain;
class CNode;
struct CNodeState;
class uint256;
class CBlockIndex;
class BlockMap;
class CAddress;

// Requires cs_main.
CNodeState* State(NodeId nodeId);
void InitializeNode(NodeId nodeid, const std::string addressName, const CAddress& addr);
void FinalizeNode(NodeId nodeid);
void UpdatePreferredDownload(CNode* node);
bool HavePreferredDownloadPeers();
int SyncStartedNodeCount();
void RecordNodeStartedToSync();
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
#endif// NODE_STATE_REGISTRY_H