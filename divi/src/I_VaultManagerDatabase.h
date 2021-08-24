#ifndef I_VAULT_MANAGER_DATABASE_H
#define I_VAULT_MANAGER_DATABASE_H
#include <map>
class CWalletTx;
class CScript;
using ManagedScripts = std::set<CScript>;
class I_VaultManagerDatabase
{
public:
    virtual ~I_VaultManagerDatabase(){}
    virtual bool WriteTx(const CWalletTx& walletTransaction) = 0;
    virtual bool ReadTx(const uint64_t txIndex, CWalletTx& walletTransaction) = 0;
    virtual bool WriteManagedScript(const CScript& managedScript) = 0;
    virtual bool EraseManagedScript(const CScript& managedScript) = 0;
    virtual bool ReadManagedScripts(ManagedScripts& managedScripts) = 0;
};
#endif// I_VAULT_MANAGER_DATABASE_H