#ifndef MASTERNODE_NETWORK_MESSAGE_MANAGER_H
#define MASTERNODE_NETWORK_MESSAGE_MANAGER_H
#include <map>
#include <uint256.h>
#include <masternode.h>
#include <sync.h>
#include <primitives/transaction.h>
#include <netbase.h>

//#include
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

    // Keep track of all broadcasts I've seen
    std::map<uint256, CMasternodeBroadcast> mapSeenMasternodeBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CMasternodePing> mapSeenMasternodePing;
};
#endif// MASTERNODE_NETWORK_MESSAGE_MANAGER_H