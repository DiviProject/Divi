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

class ChainstateManager;
class MasternodeNetworkMessageManager;
class MasternodePaymentData;
class CMasternodeConfig;
class CMasternodeMan;
class CActiveMasternode;
class I_PeerSyncQueryService;
class I_Clock;
class CNetFulfilledRequestManager;
class CAddrMan;
class StoredMasternodeBroadcasts;

class MasternodeModule
{
private:
    bool fMasterNode_;
    std::unique_ptr<CNetFulfilledRequestManager> networkFulfilledRequestManager_;
    std::unique_ptr<MasternodeNetworkMessageManager> networkMessageManager_;
    std::unique_ptr<MasternodePaymentData> masternodePaymentData_;
    std::unique_ptr<CMasternodeConfig> masternodeConfig_;
    std::unique_ptr<CActiveMasternode> activeMasternode_;
    std::unique_ptr<CMasternodeSync> masternodeSync_;
    std::unique_ptr<CMasternodeMan> mnodeman_;
    std::unique_ptr<CMasternodePayments> masternodePayments_;
    std::unique_ptr<StoredMasternodeBroadcasts> storedBroadcasts_;
public:
    MasternodeModule(
        const I_Clock& clock,
        const I_PeerSyncQueryService& peerSyncQueryService,
        const ChainstateManager& chainstate,
        CAddrMan& addressManager);
    ~MasternodeModule();

    CNetFulfilledRequestManager& getNetworkFulfilledRequestManager() const;
    MasternodeNetworkMessageManager& getNetworkMessageManager() const;
    MasternodePaymentData& getMasternodePaymentData() const;
    CMasternodeConfig& getMasternodeConfigurations() const;
    CMasternodeMan& getMasternodeManager() const;
    CActiveMasternode& getActiveMasternode() const;
    CMasternodePayments& getMasternodePayments() const;
    CMasternodeSync& getMasternodeSynchronization() const;
    StoredMasternodeBroadcasts& getStoredBroadcasts() const;
    bool localNodeIsAMasternode() const;
    void designateLocalNodeAsMasternode();
};

// Used for downstream constructors and use cases
const MasternodeModule& GetMasternodeModule();

// Used in main to manage signals back and forth
bool VoteForMasternodePayee(const CBlockIndex* pindex);
void ProcessMasternodeMessages(CNode* pfrom, std::string strCommand, CDataStream& vRecv);
bool MasternodeWinnerIsKnown(const uint256& inventoryHash);
bool MasternodeIsKnown(const uint256& inventoryHash);
bool MasternodePingIsKnown(const uint256& inventoryHash);
bool ShareMasternodeBroadcastWithPeer(CNode* peer,const uint256& inventoryHash);
bool ShareMasternodePingWithPeer(CNode* peer,const uint256& inventoryHash);
bool ShareMasternodeWinnerWithPeer(CNode* peer,const uint256& inventoryHash);

//Used in rpcmisc for manual restart of mn sync
void ForceMasternodeResync();

// Used for initialization
void ThreadMasternodeBackgroundSync();
void LockUpMasternodeCollateral(const Settings& settings, std::function<void(const COutPoint&)> walletUtxoLockingFunction);
bool LoadMasternodeDataFromDisk(UIMessenger& uiMessenger,std::string pathToDataDir);
void SaveMasternodeDataToDisk();
bool InitializeMasternodeIfRequested(const Settings& settings, bool transactionIndexEnabled, std::string& errorMessage);
#endif //MASTERNODE_MODULE_H
