#ifndef WALLET_BACKUP_FEATURE_CONTAINER_H
#define WALLET_BACKUP_FEATURE_CONTAINER_H

#include <I_Filesystem.h>
#include <I_WalletBackupCreator.h>
#include <WalletIntegrityVerifier.h>
#include <I_DatabaseWrapper.h>
#include <memory>

class WalletBackupFeatureContainer
{

private:
    std::string dataDirectory_;
    std::string walletFileName_;
    std::unique_ptr<I_FileSystem> fileSystem_;
    std::unique_ptr<I_WalletBackupCreator> walletBackupCreator_;
    std::unique_ptr<I_WalletBackupCreator> monthlyWalletBackupCreator_;
    std::unique_ptr<I_WalletBackupCreator> monthlyWalletBackupDecorator_;
    std::unique_ptr<I_DatabaseWrapper> database_;
    std::unique_ptr<WalletIntegrityVerifier> walletIntegrityVerifier_;

    bool fileToBackUpExists();
    bool backupWallet (bool monthlyBackupOnly);
public:
    WalletBackupFeatureContainer (
        int numberOfBackups,
        std::string walletFileName,
        std::string dataDirectory);

    bool createMonthlyBackup();
    bool createCurrentBackup();
};

#endif //WALLET_BACKUP_FEATURE_CONTAINER_H