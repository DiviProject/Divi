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
#endif //MASTERNODE_MODULE_H