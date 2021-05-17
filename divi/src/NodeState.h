#ifndef NODE_STATE_H
#define NODE_STATE_H
#include <vector>
#include <string>
#include <list>
#include <NodeId.h>
#include <uint256.h>
#include <netbase.h>
#include <BlockRejects.h>
#include <QueuedBlock.h>

class CBlockIndex;

/**
 * Maintain validation-specific state about nodes, protected by cs_main, instead
 * by CNode's own locks. This simplifies asynchronous operation, where
 * processing of incoming data is done after the ProcessMessage call returns,
 * and we're no longer holding the node's locks.
 */
struct CNodeState {
private:
    static int countOfNodesAlreadySyncing;
    static int numberOfPreferredDownloadSources;
public:
    NodeId nodeId;
    //! The peer's address
    CService address;
    //! Whether we have a fully established connection.
    bool fCurrentlyConnected;
    //! Accumulated misbehaviour score for this peer.
    int nMisbehavior;
    //! Whether this peer should be disconnected and banned (unless whitelisted).
    bool fShouldBan;
    //! String name of this peer (debugging/logging purposes).
    std::string name;
    //! List of asynchronously-determined block rejections to notify this peer about.
    std::vector<CBlockReject> rejects;
    //! The best known block we know this peer has announced.
    CBlockIndex* pindexBestKnownBlock;
    //! The hash of the last unknown block this peer has announced.
    uint256 hashLastUnknownBlock;
    //! The last full block we both have.
    CBlockIndex* pindexLastCommonBlock;
    //! Whether we've started headers synchronization with this peer.
    bool fSyncStarted;
    //! Since when we're stalling block download progress (in microseconds), or 0.
    int64_t nStallingSince;
    std::list<QueuedBlock> vBlocksInFlight;
    int nBlocksInFlight;
    //! Whether we consider this a preferred download peer.
    bool fPreferredDownload;

    CNodeState(NodeId nodeIdValue);
    ~CNodeState();
    void RecordNodeStartedToSync();
    void UpdatePreferredDownload(bool updatedStatus);
    static bool NodeSyncStarted();
    static bool HavePreferredDownloadPeers();
};

#endif// NODE_STATE_H