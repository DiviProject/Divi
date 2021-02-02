#ifndef MASTERNODE_NETWORK_MESSAGE_MANAGER_H
#define MASTERNODE_NETWORK_MESSAGE_MANAGER_H
#include <map>
#include <uint256.h>
#include <masternode.h>
#include <sync.h>
#include <primitives/transaction.h>
#include <netbase.h>
#include <serialize.h>

class MasternodeNetworkMessageManager
{
public:
    MasternodeNetworkMessageManager();
    // who's asked for the Masternode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForMasternodeList;
    // who we asked for the Masternode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForMasternodeList;
    // which Masternodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForMasternodeListEntry;

    // Dummy variable to keep serialization consistent;
    int64_t nDsqCount;

    // Keep track of all broadcasts I've seen
    std::map<uint256, CMasternodeBroadcast> mapSeenMasternodeBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CMasternodePing> mapSeenMasternodePing;

    void clearExpiredMasternodeListRequestsFromPeers();
    void clearExpiredMasternodeListRequestsToPeers();
    void clearTimedOutMasternodeEntryRequests();
    void clearExpiredMasternodeEntryRequests(const COutPoint& masternodeCollateral);
    bool peerHasRequestedMasternodeListTooOften(const CAddress& peerAddress);
    bool recordDsegUpdateAttempt(const CAddress& peerAddress);
    bool recordMasternodeEntryRequestAttempt(const COutPoint& masternodeCollateral);
    void clear();
    std::string ToString() const;

    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mAskedUsForMasternodeList);
        READWRITE(mWeAskedForMasternodeList);
        READWRITE(mWeAskedForMasternodeListEntry);
    }
};
#endif// MASTERNODE_NETWORK_MESSAGE_MANAGER_H