#ifndef WALLET_BACKUP_CREATOR_H
#define WALLET_BACKUP_CREATOR_H
#include <string>
#include <boost/filesystem.hpp>
#include <i_walletBackupCreator.h>
#include <i_filesystem.h>

class I_FormattedTimestampProvider;

class WalletBackupCreator : public I_WalletBackupCreator
{
private:
    int nWalletBackups = 0;
    I_FileSystem& fileSystem_;
    I_FormattedTimestampProvider& formattedTimestampProvider_;
    std::string dataDirectory_;
    std::string walletFilename_;
    std::string backupSubfolderDirectory_;
    void ClearFoldersForResync();
    bool BackupDatabaseInCaseOfError();
    bool VerifyWallet(std::string strWalletFile);
    bool BackupFile(PathType& sourceFile, PathType& backupFile);
    bool BackupWalletFile(std::string strWalletFile, PathType backupDir);
    void PruneOldBackups(std::string strWalletFile, PathType backupDir);

public:
    WalletBackupCreator (
        int numberOfBackups,
        I_FileSystem& fileSystem,
        I_FormattedTimestampProvider& formattedTimestampProvider,
        std::string dataDirectory,
        std::string walletFilename = std::string("wallet.dat"),
        std::string backupSubfolderDirectory = std::string("/backups"));

    virtual ~WalletBackupCreator(){}
    virtual bool BackupWallet();
    virtual bool CheckWalletIntegrity(bool resync);
};



#endif //WALLET_BACKUP_CREATOR_H