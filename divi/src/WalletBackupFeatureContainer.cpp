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
    , fileSystem_(new FilteredBoostFileSystem(walletFileName))
    , walletBackupCreator_(new WalletBackupCreator(numberOfBackups, *fileSystem_, dataDirectory, walletFileName, "/backups"))
    , monthlyWalletBackupCreator_(new WalletBackupCreator(numberOfBackups, *fileSystem_, dataDirectory, walletFileName, "/monthlyBackups"))
    , monthlyWalletBackupDecorator_(new MonthlyWalletBackupCreator(*monthlyWalletBackupCreator_, *fileSystem_))
    , database_(new DatabaseWrapper(dataDirectory))
    , walletIntegrityVerifier_(new WalletIntegrityVerifier(dataDirectory_,*fileSystem_, *database_))
{

}

bool WalletBackupFeatureContainer::backupWallet(bool monthlyBackupOnly)
{
    boost::filesystem::path walletPath = boost::filesystem::path(dataDirectory_) / walletFileName_;
    if(!fileSystem_->exists(walletPath.string()))
    {
        LogPrintf("Wallet file not found at %s. Skipping backup...\n",walletPath.string());
        return true;
    }

    LOCK(database_->GetDatabaseLock());
    if (!database_->FilenameIsInUse(walletFileName_))
    {
        // Flush log data to the dat file
        database_->FlushToDisk(walletFileName_);
        LogPrintf("backing up wallet\n");
        if(walletIntegrityVerifier_->CheckWalletIntegrity(walletFileName_))
        {
            if(monthlyBackupOnly) monthlyWalletBackupDecorator_->BackupWallet();
            if(!monthlyBackupOnly) walletBackupCreator_->BackupWallet();
        }
        else
        {
            LogPrintf("Error: Wallet integrity check failed.");
        }
        return true;
    }

    return false; // Keep trying
}
bool WalletBackupFeatureContainer::createMonthlyBackup()
{
    return backupWallet(true);
}
bool WalletBackupFeatureContainer::createCurrentBackup()
{
    return backupWallet(false);
}