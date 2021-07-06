#ifndef RPC_MASTERNODE_FEATURES_H
#define RPC_MASTERNODE_FEATURES_H
#include <string>
#include <vector>
#include <stdint.h>
class CKeyStore;
class CBlockIndex;
struct MasternodeStartResult
{
    bool status;
    std::string broadcastData;
    std::string errorMessage;
    MasternodeStartResult();
};
struct ActiveMasternodeStatus
{
    bool activeMasternodeFound;
    std::string txHash;
    std::string outputIndex;
    std::string netAddress;
    std::string collateralAddress;
    std::string statusCode;
    std::string statusMessage;
    ActiveMasternodeStatus();
};
struct MasternodeListEntry
{
    std::string network;
    std::string txHash;
    uint64_t outputIndex;
    std::string status;
    std::string collateralAddress;
    int protocolVersion;
    int64_t signatureTime;
    int64_t lastSeenTime;
    int64_t activeTime;
    int64_t lastPaidTime;
    std::string masternodeTier;
    MasternodeListEntry();
};
struct MasternodeCountData
{
    int total;
    int stable;
    int enabledAndActive;
    int enabled;
    int queueCount;
    int ipv4;
    int ipv6;
    int onion;
    MasternodeCountData();
};

/** Relays a broadcast given in serialised form as hex string.  If the signature
 *  is present, then it will replace the signature in the broadcast.  If
 *  updatePing is true, then the masternode ping is re-signed freshly.  */
MasternodeStartResult RelayMasternodeBroadcast(const std::string& hexData, const std::string& signature, bool updatePing);
MasternodeStartResult StartMasternode(const CKeyStore& keyStore, std::string alias, bool deferRelay);
ActiveMasternodeStatus GetActiveMasternodeStatus();
std::vector<MasternodeListEntry> GetMasternodeList(std::string strFilter);
MasternodeCountData GetMasternodeCounts(const CBlockIndex* chainTip);
#endif// RPC_MASTERNODE_FEATURES_H
