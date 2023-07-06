#ifndef MOCK_VAULT_MANAGER_DATABASE_H
#define MOCK_VAULT_MANAGER_DATABASE_H
#include <gmock/gmock.h>
#include <I_VaultManagerDatabase.h>
class MockVaultManagerDatabase: public I_VaultManagerDatabase
{
public:
    MOCK_METHOD1(WriteTx,bool(const CWalletTx& walletTransaction));
    MOCK_METHOD1(ReadTx,bool(CWalletTx& walletTransaction));
    MOCK_METHOD1(WriteManagedScript,bool(const CScript& managedScript));
    MOCK_METHOD1(EraseManagedScript,bool(const CScript& managedScript));
    MOCK_METHOD1(ReadManagedScripts, bool(ManagedScripts& managedScripts));
    MOCK_METHOD1(Sync, bool(bool forceSync));
};
#endif// MOCK_VAULT_MANAGER_DATABASE_H
