#include <NodeState.h>

CNodeState::CNodeState(
    ): fCurrentlyConnected(false)
    , nMisbehavior(0)
    , fShouldBan(false)
    , pindexBestKnownBlock(nullptr)
    , hashLastUnknownBlock(uint256(0))
    , pindexLastCommonBlock(nullptr)
    , fSyncStarted(false)
    , nStallingSince(0)
    , nBlocksInFlight(0)
    , fPreferredDownload(false)
{
}