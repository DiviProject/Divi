#ifndef WALLET_INTEGRITY_VERIFIER_H
#define WALLET_INTEGRITY_VERIFIER_H
#include <string>
class I_FileSystem;
class I_DatabaseWrapper;

class WalletIntegrityVerifier
{
private:
    I_FileSystem& fileSystem_;
    I_DatabaseWrapper& dbInterface_;
    unsigned backupCount_;

    bool backupDatabaseIfUnavailable(
        const std::string& dataDirectory);
public:
    WalletIntegrityVerifier(
        I_FileSystem& fileSystem, 
        I_DatabaseWrapper& database);
    bool CheckWalletIntegrity(
        const std::string& dataDirectory,
        const std::string& walletFilename);
};
#endif //WALLET_INTEGRITY_VERIFIER_H