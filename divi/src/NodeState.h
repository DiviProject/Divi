#ifndef NODE_STATE_H
#define NODE_STATE_H
#include <vector>
#include <string>
#include <list>
#include <NodeId.h>
#include <uint256.h>
#include <netbase.h>

class CBlockIndex;
class CAddrMan;
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
    CAddrMan& addressManager_;
    int banThreshold_;

    //! Accumulated misbehaviour score for this peer.
    int nMisbehavior;
    //! Whether we've started headers synchronization with this peer.
    bool fSyncStarted;
public:
    NodeId nodeId;
    //! The peer's address
    CService address;
    //! Whether we have a fully established connection.
    bool fCurrentlyConnected;
    //! Whether this peer should be disconnected and banned (unless whitelisted).
    bool fShouldBan;
    //! String name of this peer (debugging/logging purposes).
    std::string name;
    //! The best known block we know this peer has announced.
    const CBlockIndex* pindexBestKnownBlock;
    //! The hash of the last unknown block this peer has announced.
    uint256 hashLastUnknownBlock;
    //! The last full block we both have.
    const CBlockIndex* pindexLastCommonBlock;
    //! Whether we consider this a preferred download peer.
    bool fPreferredDownload;

    CNodeState(NodeId nodeIdValue,CAddrMan& addressManager);
    ~CNodeState();
    bool Syncing() const;
    void RecordNodeStartedToSync();
    void UpdatePreferredDownload(bool updatedStatus);
    static bool NodeSyncStarted();
    static bool HavePreferredDownloadPeers();
    void Finalize();
    void ApplyMisbehavingPenalty(int penaltyAmount,std::string cause);
    int GetMisbehaviourPenalty() const;
    void SetBanScoreThreshold(int banThreshold);
};

#endif// NODE_STATE_H
