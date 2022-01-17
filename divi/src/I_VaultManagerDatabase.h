#ifndef I_VAULT_MANAGER_DATABASE_H
#define I_VAULT_MANAGER_DATABASE_H
#include <set>
class CCriticalSection;
class CWalletTx;
class CScript;
using ManagedScripts = std::set<CScript>;
class I_VaultManagerDatabase
{
public:
    virtual ~I_VaultManagerDatabase(){}
    virtual bool WriteTx(const CWalletTx& walletTransaction) = 0;
    virtual bool ReadTx(CWalletTx& walletTransaction) = 0;
    virtual bool WriteManagedScript(const CScript& managedScript) = 0;
    virtual bool EraseManagedScript(const CScript& managedScript) = 0;
    virtual bool ReadManagedScripts(ManagedScripts& managedScripts) = 0;
    virtual bool Sync(CCriticalSection& mutexToLock) = 0;
};
#endif// I_VAULT_MANAGER_DATABASE_H