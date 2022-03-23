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

bool WalletBackupFeatureContainer::backupWalletWithLockedDB(bool monthlyBackupOnly)
{
    database_->Lock();
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
        database_->Unlock();
        return true;
    }
    database_->Unlock();
    return false; // Keep trying
}

bool WalletBackupFeatureContainer::backupWallet(bool monthlyBackupOnly)
{
    if(fileToBackUpExists())
    {
        const bool result = backupWalletWithLockedDB(monthlyBackupOnly);
        return result;
    }
    else
    {
        return true;
    }
}

bool WalletBackupFeatureContainer::createMonthlyBackup()
{
    return backupWallet(true);
}
bool WalletBackupFeatureContainer::createCurrentBackup()
{
    return backupWallet(false);
}