#ifndef WALLET_BACKUP_CREATOR_H
#define WALLET_BACKUP_CREATOR_H
#include <string>
#include <boost/filesystem.hpp>
#include <i_filesystem.h>


class WalletBackupCreator
{
private:
    int nWalletBackups = 0;
    I_FileSystem& fileSystem_;
    std::string dataDirectory_;
    void ClearFoldersForResync();
    bool BackupDatabaseInCaseOfError();
    bool VerifyWallet(std::string strWalletFile);
    void BackupFile(PathType& sourceFile, PathType& backupFile);
    void BackupWalletFile(std::string strWalletFile, PathType backupDir);
    void PruneOldBackups(std::string strWalletFile, PathType backupDir);
    TimeStampedFilePaths RecordTimestamps(PathType backupDir);
public:
    WalletBackupCreator (
        int numberOfBackups,
        I_FileSystem& fileSystem,
        std::string dataDirectory
        ): nWalletBackups(numberOfBackups)
        , fileSystem_(fileSystem)
        , dataDirectory_(dataDirectory)
    {
        nWalletBackups = std::max(0, std::min(10, nWalletBackups));
    }
    bool BackupWallet(std::string strDataDir, bool fDisableWallet);
};

#endif //WALLET_BACKUP_CREATOR_H