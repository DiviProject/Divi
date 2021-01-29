#ifndef MASTERNODE_MODULE_H
#define MASTERNODE_MODULE_H
#include <string>
class Settings;
void ThreadMasternodeBackgroundSync();
bool SetupActiveMasternode(const Settings& settings, std::string& errorMessage);
#endif //MASTERNODE_MODULE_H