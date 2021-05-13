#ifndef NODE_STATE_REGISTRY_H
#define NODE_STATE_REGISTRY_H
#include <NodeId.h>
class CNode;
struct CNodeState;
class uint256;
class CBlockIndex;
class BlockMap;

// Requires cs_main.
CNodeState* State(NodeId nodeId);
void InitializeNode(NodeId nodeid, const CNode* pnode);
void FinalizeNode(NodeId nodeid);
void UpdatePreferredDownload(CNode* node, CNodeState* state);
bool HavePreferredDownloadPeers();
int SyncStartedNodeCount();
void RecordNodeStartedToSync();
void MarkBlockAsReceived(const uint256& hash);
void MarkBlockAsInFlight(NodeId nodeid, const uint256& hash, CBlockIndex* pindex = nullptr);
bool BlockIsInFlight(const uint256& hash);
NodeId GetSourceOfInFlightBlock(const uint256& hash);
void ProcessBlockAvailability(const BlockMap& blockIndicesByHash, NodeId nodeid);
#endif// NODE_STATE_REGISTRY_H