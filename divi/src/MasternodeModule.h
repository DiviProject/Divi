#ifndef MASTERNODE_MODULE_H
#define MASTERNODE_MODULE_H
#include <string>
#include <primitives/transaction.h>
#include <functional>
#include <stdint.h>
#include <vector>
class Settings;
class CBlockIndex;
class CDataStream;
class CNode;
class CMasternodeSync;
class UIMessenger;
class CMasternodePayments;

void ThreadMasternodeBackgroundSync();
bool SetupActiveMasternode(const Settings& settings, std::string& errorMessage);
void LockUpMasternodeCollateral(const Settings& settings, std::function<void(const COutPoint&)> walletUtxoLockingFunction);
bool VoteForMasternodePayee(const CBlockIndex* pindex);
void ProcessMasternodeMessages(CNode* pfrom, std::string strCommand, CDataStream& vRecv);
bool MasternodeWinnerIsKnown(const uint256& inventoryHash);
bool MasternodeIsKnown(const uint256& inventoryHash);
bool MasternodePingIsKnown(const uint256& inventoryHash);
bool ShareMasternodeBroadcastWithPeer(CNode* peer,const uint256& inventoryHash);
bool ShareMasternodePingWithPeer(CNode* peer,const uint256& inventoryHash);
void ForceMasternodeResync();
const CMasternodeSync& GetMasternodeSync();
bool RelayMasternodeBroadcast(std::string hexData,std::string signature = "");
struct MasternodeStartResult
{
    bool status;
    std::string broadcastData;
    std::string errorMessage;
    MasternodeStartResult(
        ): status(false)
        , broadcastData("")
        , errorMessage("No Error Data")
    {}
};
MasternodeStartResult StartMasternode(std::string alias, bool deferRelay);
struct ActiveMasternodeStatus
{
    bool activeMasternodeFound;
    std::string txHash;
    std::string outputIndex;
    std::string netAddress;
    std::string collateralAddress;
    std::string statusCode;
    std::string statusMessage;
    ActiveMasternodeStatus(
        ): activeMasternodeFound(false)
        , txHash()
        , outputIndex()
        , netAddress()
        , collateralAddress()
        , statusCode()
        , statusMessage()
    {}
};
ActiveMasternodeStatus GetActiveMasternodeStatus();
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
    MasternodeListEntry(
        ): network()
        , txHash()
        , outputIndex()
        , status()
        , collateralAddress()
        , protocolVersion()
        , signatureTime()
        , lastSeenTime()
        , activeTime()
        , lastPaidTime()
        , masternodeTier()
    {}
};
std::vector<MasternodeListEntry> GetMasternodeList(std::string strFilter);
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
    MasternodeCountData(){}
};
MasternodeCountData GetMasternodeCounts(const CBlockIndex* chainTip);
bool LoadMasternodeDataFromDisk(UIMessenger& uiMessenger,std::string pathToDataDir);
void DumpMasternodeDataToDisk();
CMasternodePayments& GetMasternodePayments();
#endif //MASTERNODE_MODULE_H