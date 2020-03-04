#ifndef WALLET_BACKUP_CREATOR_H
#define WALLET_BACKUP_CREATOR_H
#include <string>
#include <boost/filesystem.hpp>


class WalletBackupCreator
{
private:
    int nWalletBackups = 0;
    void ClearFoldersForResync();
    bool BackupDatabase();
    bool VerifyWallet(std::string strWalletFile);
    void BackupFile(boost::filesystem::path& sourceFile,boost::filesystem::path& backupFile);
    void BackupWalletFile(std::string strWalletFile, boost::filesystem::path backupDir);
    void PruneOldBackups(std::string strWalletFile, boost::filesystem::path backupDir);
public:
    WalletBackupCreator (int numberOfBackups) : nWalletBackups(numberOfBackups) {}
    bool BackupWallet(std::string strDataDir, bool fDisableWallet);
};

#endif //WALLET_BACKUP_CREATOR_H