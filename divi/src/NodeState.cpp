#include <NodeState.h>

#include <Logging.h>
#include <Settings.h>
extern Settings& settings;

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

/** Map maintaining per-node state. Requires cs_main. */
std::map<NodeId, CNodeState> mapNodeState;
// Requires cs_main.
CNodeState* State(int pnode)
{
    std::map<NodeId, CNodeState>::iterator it = mapNodeState.find(pnode);
    if (it == mapNodeState.end())
        return NULL;
    return &it->second;
}

void Misbehaving(int pnode, int howmuch)
{
    if (howmuch == 0)
        return;

    CNodeState* state = State(pnode);
    if (state == NULL)
        return;

    state->nMisbehavior += howmuch;
    int banscore = settings.GetArg("-banscore", 100);
    if (state->nMisbehavior >= banscore && state->nMisbehavior - howmuch < banscore) {
        LogPrintf("Misbehaving: %s (%d -> %d) BAN THRESHOLD EXCEEDED\n", state->name, state->nMisbehavior - howmuch, state->nMisbehavior);
        state->fShouldBan = true;
    } else
        LogPrintf("Misbehaving: %s (%d -> %d)\n", state->name, state->nMisbehavior - howmuch, state->nMisbehavior);
}