#ifndef MASTERNODE_MODULE_H
#define MASTERNODE_MODULE_H
#include <string>
#include <primitives/transaction.h>
#include <functional>
class Settings;
class CBlockIndex;
class CDataStream;
class CNode;
class CMasternodeSync;
void ThreadMasternodeBackgroundSync();
bool SetupActiveMasternode(const Settings& settings, std::string& errorMessage);
void LockUpMasternodeCollateral(const Settings& settings, std::function<void(const COutPoint&)> walletUtxoLockingFunction);
bool VoteForMasternodePayee(const CBlockIndex* pindex);
void ProcessMasternodeMessages(CNode* pfrom, std::string strCommand, CDataStream& vRecv);
bool MasternodeWinnerIsKnown(const uint256& inventoryHash);
bool MasternodeIsKnown(const uint256& inventoryHash);
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
#endif //MASTERNODE_MODULE_H