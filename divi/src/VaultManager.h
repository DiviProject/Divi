#ifndef VAULT_MANAGER_H
#define VAULT_MANAGER_H
#include <vector>
#include <map>
#include <set>
class CScript;
using ManagedScripts = std::map<CScript,unsigned>;
class COutPoint;
using UnspentOutputs = std::set<COutPoint>;
class CTransaction;
class CBlock;
class CWalletTx;
class uint256;
using TransactionsByHash = std::map<uint256,CWalletTx>;

class VaultManager
{
private:
    ManagedScripts managedScriptsLimits_;
    TransactionsByHash transactionsByHash_;
public:
    VaultManager();
    ~VaultManager();
    void SyncTransaction(const CTransaction& tx, const CBlock *pblock);
    void addManagedScript(const CScript& script, unsigned limit);
    UnspentOutputs getUTXOs() const;
};
#endif// VAULT_MANAGER_H