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
void InitializeNode(CNodeState& nodeState);
void FinalizeNode(NodeId nodeId);
void MarkBlockAsReceived(const uint256& hash);
void MarkBlockAsInFlight(NodeId nodeid, const uint256& hash, const CBlockIndex* pindex = nullptr);
bool BlockIsInFlight(const uint256& hash);
void UpdateBlockAvailability(const BlockMap& blockIndicesByHash, CNodeState* state, const uint256& hash);
void FindNextBlocksToDownload(
    const BlockMap& blockIndicesByHash,
    const CChain& activeChain,
    CNodeState* state,
    unsigned int count,
    std::vector<const CBlockIndex*>& vBlocks,
    NodeId& nodeStaller);

bool BlockDownloadHasTimedOut(NodeId nodeId, int64_t nNow, int64_t targetSpacing);
bool BlockDownloadHasStalled(NodeId nodeId, int64_t nNow, int64_t stallingWindow);
void RecordWhenStallingBegan(NodeId nodeId, int64_t currentTimestamp);
std::vector<int> GetBlockHeightsInFlight(NodeId nodeId);
int GetNumberOfBlocksInFlight(NodeId nodeId);
// Requires cs_main.
/** Increase a node's misbehavior score. */
bool Misbehaving(NodeId nodeId, int howmuch, std::string cause);
bool Misbehaving(CNodeState* state, int howmuch, std::string cause);
#endif// NODE_STATE_REGISTRY_H
