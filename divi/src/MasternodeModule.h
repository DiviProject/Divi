#ifndef MASTERNODE_MODULE_H
#define MASTERNODE_MODULE_H
#include <string>
#include <primitives/transaction.h>
#include <functional>
#include <stdint.h>
#include <vector>
#include <memory>

class Settings;
class CBlockIndex;
class CDataStream;
class CNode;
class CMasternodeSync;
class UIMessenger;
class CMasternodePayments;
class CKeyStore;

class CChain;
class BlockMap;
class MasternodeNetworkMessageManager;
class MasternodePaymentData;
class CMasternodeConfig;
class CMasternodeMan;
class CActiveMasternode;
class MasternodeModule
{
private:
    bool fMasterNode_;
    const CChain& activeChain_;
    const BlockMap& blockIndexByHash_;
    std::unique_ptr<MasternodeNetworkMessageManager> networkMessageManager_;
    std::unique_ptr<MasternodePaymentData> masternodePaymentData_;
    std::unique_ptr<CMasternodeConfig> masternodeConfig_;
    std::unique_ptr<CMasternodeMan> mnodeman_;
    std::unique_ptr<CActiveMasternode> activeMasternode_;
    std::unique_ptr<CMasternodePayments> masternodePayments_;
    std::unique_ptr<CMasternodeSync> masternodeSync_;
public:
    MasternodeModule(
        const CChain& activeChain,
        const BlockMap& blockIndexByHash);
    ~MasternodeModule();
    MasternodeNetworkMessageManager& getNetworkMessageManager() const;
    MasternodePaymentData& getMasternodePaymentData() const;
    CMasternodeConfig& getMasternodeConfigurations() const;
    CMasternodeMan& getMasternodeManager() const;
    CActiveMasternode& getActiveMasternode() const;
    CMasternodePayments& getMasternodePayments() const;
    CMasternodeSync& getMasternodeSynchronization() const;
    bool localNodeIsAMasternode() const;
    void designateLocalNodeAsMasternode();
};

const CMasternodeSync& GetMasternodeSync();
CMasternodePayments& GetMasternodePayments();

void ThreadMasternodeBackgroundSync();
void LockUpMasternodeCollateral(const Settings& settings, std::function<void(const COutPoint&)> walletUtxoLockingFunction);
bool VoteForMasternodePayee(const CBlockIndex* pindex);
void ProcessMasternodeMessages(CNode* pfrom, std::string strCommand, CDataStream& vRecv);
bool MasternodeWinnerIsKnown(const uint256& inventoryHash);
bool MasternodeIsKnown(const uint256& inventoryHash);
bool MasternodePingIsKnown(const uint256& inventoryHash);
bool ShareMasternodeBroadcastWithPeer(CNode* peer,const uint256& inventoryHash);
bool ShareMasternodePingWithPeer(CNode* peer,const uint256& inventoryHash);
bool ShareMasternodeWinnerWithPeer(CNode* peer,const uint256& inventoryHash);
void ForceMasternodeResync();
bool LoadMasternodeDataFromDisk(UIMessenger& uiMessenger,std::string pathToDataDir);
void DumpMasternodeDataToDisk();
bool InitializeMasternodeIfRequested(const Settings& settings, bool transactionIndexEnabled, std::string& errorMessage);
#endif //MASTERNODE_MODULE_H