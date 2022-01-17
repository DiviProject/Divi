#ifndef VAULT_MANAGER_DATABASE_H
#define VAULT_MANAGER_DATABASE_H
#include <I_VaultManagerDatabase.h>
#include <leveldbwrapper.h>
#include <destination.h>
#include <uint256.h>

class VaultManagerDatabase final: public I_VaultManagerDatabase, public CLevelDBWrapper
{
private:
    uint64_t txCount;
    std::map<uint256,uint64_t> txidLookup;
    std::map<CScriptID,uint64_t> scriptIDLookup;

public:
    VaultManagerDatabase(std::string vaultID, size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    bool WriteTx(const CWalletTx& walletTransaction) override;
    bool ReadTx(CWalletTx& walletTransaction) override;
    bool WriteManagedScript(const CScript& managedScript) override;
    bool EraseManagedScript(const CScript& managedScript) override;
    bool ReadManagedScripts(ManagedScripts& managedScripts) override;
    bool Sync() override;
};
#endif// VAULT_MANAGER_DATABASE_H