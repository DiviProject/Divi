#ifndef WALLET_INTEGRITY_VERIFIER_H
#define WALLET_INTEGRITY_VERIFIER_H
#include <string>
class I_FileSystem;
class I_DatabaseWrapper;

class WalletIntegrityVerifier
{
private:
    const std::string& dataDirectory_;
    I_FileSystem& fileSystem_;
    I_DatabaseWrapper& dbInterface_;
    unsigned backupCount_;

    bool backupDatabaseIfUnavailable();
public:
    WalletIntegrityVerifier(
        const std::string& dataDirectory,
        I_FileSystem& fileSystem,
        I_DatabaseWrapper& database);
    bool CheckWalletIntegrity(
        const std::string& walletFilename);
};
#endif //WALLET_INTEGRITY_VERIFIER_H