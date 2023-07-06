#include <WalletBackupFeatureContainer.h>
#include <FilteredBoostFileSystem.h>
#include <WalletBackupCreator.h>
#include <MonthlyWalletBackupCreator.h>
#include <DatabaseWrapper.h>
#include <Logging.h>

WalletBackupFeatureContainer::WalletBackupFeatureContainer (
        int numberOfBackups,
        std::string walletFileName,
        std::string dataDirectory
    ): dataDirectory_(dataDirectory)
    , walletFileName_(walletFileName)
    , fileSystem_(new FilteredBoostFileSystem(walletFileName_))
    , walletBackupCreator_(new WalletBackupCreator(numberOfBackups, *fileSystem_, dataDirectory_, walletFileName_, "/backups"))
    , monthlyWalletBackupCreator_(new WalletBackupCreator(numberOfBackups, *fileSystem_, dataDirectory_, walletFileName_, "/monthlyBackups"))
    , monthlyWalletBackupDecorator_(new MonthlyWalletBackupCreator(*monthlyWalletBackupCreator_, *fileSystem_))
    , database_(new DatabaseWrapper(dataDirectory_))
    , walletIntegrityVerifier_(new WalletIntegrityVerifier(dataDirectory_,*fileSystem_, *database_))
{

}

bool WalletBackupFeatureContainer::fileToBackUpExists()
{
    boost::filesystem::path walletPath = boost::filesystem::path(dataDirectory_) / walletFileName_;
    if(!fileSystem_->exists(walletPath.string()))
    {
        LogPrintf("Wallet file not found at %s. Skipping backup...\n",walletPath.string());
        return false;
    }
    return true;
}

WalletBackupFeatureContainer::BackupStatus WalletBackupFeatureContainer::backupWalletWithLockedDB(bool monthlyBackupOnly)
{
    if (!database_->FilenameIsInUse(walletFileName_))
    {
        // Flush log data to the dat file
        database_->FlushToDisk(walletFileName_);
        LogPrintf("backing up wallet\n");
        if(walletIntegrityVerifier_->CheckWalletIntegrity(walletFileName_))
        {
            if(monthlyBackupOnly && monthlyWalletBackupDecorator_->BackupWallet()) return BackupStatus::BACKUP_CREATED;
            if(!monthlyBackupOnly && walletBackupCreator_->BackupWallet()) return BackupStatus::BACKUP_CREATED;
            return BackupStatus::BACKUP_ATTEMPTED;
        }
        else
        {
            LogPrintf("Error: Wallet integrity check failed.");
            return BackupStatus::INTEGRITY_CHECK_FAILED;
        }
    }
    return BackupStatus::FILE_IN_USE;
}

WalletBackupFeatureContainer::BackupStatus WalletBackupFeatureContainer::backupWallet(bool monthlyBackupOnly)
{
    if(fileToBackUpExists())
    {
        database_->Lock();
        const BackupStatus result = backupWalletWithLockedDB(monthlyBackupOnly);
        database_->Unlock();
        return result;
    }
    else
    {
        return BackupStatus::NO_FILE_TO_BACKUP;
    }
}

WalletBackupFeatureContainer::BackupStatus WalletBackupFeatureContainer::createMonthlyBackup()
{
    return backupWallet(true);
}
WalletBackupFeatureContainer::BackupStatus WalletBackupFeatureContainer::createCurrentBackup()
{
    return backupWallet(false);
}