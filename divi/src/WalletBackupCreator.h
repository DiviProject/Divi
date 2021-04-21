#ifndef WALLET_BACKUP_CREATOR_H
#define WALLET_BACKUP_CREATOR_H
#include <string>
#include <boost/filesystem.hpp>
#include <i_walletBackupCreator.h>
#include <i_filesystem.h>

class WalletBackupCreator : public I_WalletBackupCreator
{
private:
    int nWalletBackups = 0;
    I_FileSystem& fileSystem_;
    std::string dataDirectory_;
    std::string walletFilename_;
    std::string backupSubfolderDirectory_;
    bool BackupFile(PathType& sourceFile, PathType& backupFile);
    bool BackupWalletFile(std::string strWalletFile, PathType backupDir);
    void PruneOldBackups(std::string strWalletFile, PathType backupDir);

public:
    WalletBackupCreator (
        int numberOfBackups,
        I_FileSystem& fileSystem,
        std::string dataDirectory,
        std::string walletFilename = std::string("wallet.dat"),
        std::string backupSubfolderDirectory = std::string("/backups"));

    virtual ~WalletBackupCreator(){}
    virtual bool BackupWallet();
    virtual std::string GetBackupSubfolderDirectory() const;
};



#endif //WALLET_BACKUP_CREATOR_H