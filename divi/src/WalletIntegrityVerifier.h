#ifndef WALLET_INTEGRITY_VERIFIER_H
#define WALLET_INTEGRITY_VERIFIER_H
class I_FileSystem;
class I_DatabaseWrapper;

class WalletIntegrityVerifier
{
private:
    I_FileSystem& fileSystem_;
    I_DatabaseWrapper& dbInterface_;
public:
    WalletIntegrityVerifier(I_FileSystem& fileSystem, I_DatabaseWrapper& database);
    bool CheckWalletIntegrity();
};
#endif //WALLET_INTEGRITY_VERIFIER_H