#ifndef MASTERNODE_MODULE_H
#define MASTERNODE_MODULE_H
#include <string>
#include <primitives/transaction.h>
#include <functional>
class Settings;
void ThreadMasternodeBackgroundSync();
bool SetupActiveMasternode(const Settings& settings, std::string& errorMessage);
void LockUpMasternodeCollateral(const Settings& settings, std::function<void(const COutPoint&)> walletUtxoLockingFunction);
#endif //MASTERNODE_MODULE_H