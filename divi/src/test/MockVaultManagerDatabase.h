#ifndef MOCK_VAULT_MANAGER_DATABASE_H
#define MOCK_VAULT_MANAGER_DATABASE_H
#include <gmock/gmock.h>
#include <I_VaultManagerDatabase.h>
class MockVaultManagerDatabase: public I_VaultManagerDatabase
{
public:
    MOCK_METHOD1(WriteTx,bool(const CWalletTx& walletTransaction));
    MOCK_METHOD1(ReadTx,bool(const uint64_t txIndex, CWalletTx& walletTransaction));
    MOCK_METHOD1(WriteManagedScripts,bool(const ManagedScripts& managedScripts));
    MOCK_METHOD1(ReadManagedScripts, bool(ManagedScripts& managedScripts));
};
#endif// MOCK_VAULT_MANAGER_DATABASE_H
