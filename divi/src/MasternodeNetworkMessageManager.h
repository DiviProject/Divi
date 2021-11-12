#ifndef MASTERNODE_NETWORK_MESSAGE_MANAGER_H
#define MASTERNODE_NETWORK_MESSAGE_MANAGER_H
#include <map>
#include <uint256.h>
#include <sync.h>
#include <primitives/transaction.h>
#include <netbase.h>
#include <serialize.h>
#include <protocol.h>
#include <masternode.h>

class CMasternodeSync;

class MasternodeNetworkMessageManager
{
private:
    // Dummy variable to keep serialization consistent;
    int64_t nDsqCount;
    // who's asked for the Masternode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForMasternodeList;
    // who we asked for the Masternode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForMasternodeList;
    // which Masternodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForMasternodeListEntry;
    // Keep track of all broadcasts I've seen
    std::map<uint256, CMasternodeBroadcast> mapSeenMasternodeBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CMasternodePing> mapSeenMasternodePing;

    void clearTimedOutMasternodeListRequestsFromPeers();
    void clearTimedOutMasternodeListRequestsToPeers();
    void clearTimedOutMasternodeEntryRequests();
    void clearTimedOutMasternodePings();
    void clearTimedOutMasternodeBroadcasts();
    void clearExpiredMasternodeBroadcasts(const COutPoint& collateral);
    void clearExpiredMasternodeEntryRequests(const COutPoint& masternodeCollateral);
public:
    std::map<uint256, int> mapSeenSyncMNB;
    std::map<uint256, int> mapSeenSyncMNW;

    mutable CCriticalSection cs;
    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    MasternodeNetworkMessageManager();
    void Clear();
    void CheckAndRemove(){}
    std::string ToString() const;

    std::vector<CMasternode> masternodes;

    uint32_t masternodeCount() const;
    const std::vector<CMasternode>& GetFullMasternodeVector() const;
    void clearTimedOutAndExpiredRequests(bool forceExpiredRemoval);
    bool peerHasRequestedMasternodeListTooOften(const CAddress& peerAddress);
    bool recordDsegUpdateAttempt(const CAddress& peerAddress);
    bool recordMasternodeEntryRequestAttempt(const COutPoint& masternodeCollateral);
    bool broadcastIsKnown(const uint256& broadcastHash) const;
    bool pingIsKnown(const uint256& pingHash) const;
    void recordLastPing(const CMasternode& mn);
    void recordBroadcast(const CMasternodeBroadcast& mnb);
    const CMasternodeBroadcast& getKnownBroadcast(const uint256& broadcastHash) const;
    const CMasternodePing& getKnownPing(const uint256& pingHash) const;
    const CMasternode* find(const CTxIn& vin) const;

    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);
        READWRITE(masternodes);
        READWRITE(mAskedUsForMasternodeList);
        READWRITE(mWeAskedForMasternodeList);
        READWRITE(mWeAskedForMasternodeListEntry);
        READWRITE(nDsqCount);
        READWRITE(mapSeenMasternodeBroadcast);
        READWRITE(mapSeenMasternodePing);
    }
};
#endif// MASTERNODE_NETWORK_MESSAGE_MANAGER_H