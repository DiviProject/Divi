#ifndef WALLET_BACKUP_CREATOR_H
#define WALLET_BACKUP_CREATOR_H
#include <string>
#include <boost/filesystem.hpp>
#include <i_filesystem.h>

class I_FormattedTimestampProvider;

class WalletBackupCreator
{
private:
    int nWalletBackups = 0;
    I_FileSystem& fileSystem_;
    I_FormattedTimestampProvider& formattedTimestampProvider_;
    std::string dataDirectory_;
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
        std::string dataDirectory
        ): nWalletBackups(numberOfBackups)
        , fileSystem_(fileSystem)
        , formattedTimestampProvider_(formattedTimestampProvider)
        , dataDirectory_(dataDirectory)
    {
        nWalletBackups = std::max(0, std::min(10, nWalletBackups));
    }
    bool BackupWallet();
    bool CheckWalletIntegrity(bool resync = false);
};



#endif //WALLET_BACKUP_CREATOR_H