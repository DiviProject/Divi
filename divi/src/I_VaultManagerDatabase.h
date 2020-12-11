#ifndef I_VAULT_MANAGER_DATABASE_H
#define I_VAULT_MANAGER_DATABASE_H
#include <map>
class CWalletTx;
class CScript;
using ManagedScripts = std::map<CScript,unsigned>;
class I_VaultManagerDatabase
{
public:
    virtual ~I_VaultManagerDatabase(){}
    virtual bool WriteTx(const CWalletTx& walletTransaction) = 0;
    virtual bool ReadTx(const uint64_t txIndex, CWalletTx& walletTransaction) = 0;
    virtual bool WriteManagedScripts(const ManagedScripts& managedScripts) = 0;
    virtual bool ReadManagedScripts(ManagedScripts& managedScripts) = 0;
};
#endif// I_VAULT_MANAGER_DATABASE_H