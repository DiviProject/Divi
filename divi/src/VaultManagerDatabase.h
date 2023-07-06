#ifndef VAULT_MANAGER_DATABASE_H
#define VAULT_MANAGER_DATABASE_H
#include <I_VaultManagerDatabase.h>
#include <leveldbwrapper.h>
#include <destination.h>
#include <uint256.h>
#include <sync.h>

class VaultManagerDatabase final: public I_VaultManagerDatabase, private CLevelDBWrapper
{
private:
    CCriticalSection cs_database;
    uint64_t txCount;
    uint64_t scriptCount;
    uint64_t updateCount_;
    uint64_t lastUpdateCount_;

public:
    VaultManagerDatabase(std::string vaultID, size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    bool WriteTx(const CWalletTx& walletTransaction) override;
    bool ReadTx(CWalletTx& walletTransaction) override;
    bool WriteManagedScript(const CScript& managedScript) override;
    bool EraseManagedScript(const CScript& managedScript) override;
    bool ReadManagedScripts(ManagedScripts& managedScripts) override;
    bool Sync(bool forceSync) override;
};
#endif// VAULT_MANAGER_DATABASE_H