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
    ): fileSystem_(new FilteredBoostFileSystem(walletFileName))
    , walletBackupCreator_(new WalletBackupCreator(numberOfBackups, *fileSystem_, dataDirectory, walletFileName, "/backups"))
    , monthlyWalletBackupCreator_(new WalletBackupCreator(numberOfBackups, *fileSystem_, dataDirectory, walletFileName, "/monthlyBackups"))
    , monthlyWalletBackupDecorator_(new MonthlyWalletBackupCreator(*monthlyWalletBackupCreator_, *fileSystem_))
    , database_(new DatabaseWrapper())
    , walletIntegrityVerifier_(new WalletIntegrityVerifier(*fileSystem_, *database_))
    , dataDirectory_(dataDirectory)
    , walletFileName_(walletFileName)
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
        database_->Dettach(walletFileName_);
        LogPrintf("backing up wallet\n");
        if(walletIntegrityVerifier_->CheckWalletIntegrity(dataDirectory_, walletFileName_))
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

I_FileSystem& WalletBackupFeatureContainer::GetFileSystem()
{
    return *fileSystem_;
}

I_WalletBackupCreator& WalletBackupFeatureContainer::GetBackupCreator ()
{
    return *walletBackupCreator_;
}

I_DatabaseWrapper& WalletBackupFeatureContainer::GetDatabase ()
{
    return *database_;
}

I_WalletBackupCreator& WalletBackupFeatureContainer::GetMonthlyBackupCreator ()
{
    return *monthlyWalletBackupDecorator_;
}

WalletIntegrityVerifier& WalletBackupFeatureContainer::GetWalletIntegrityVerifier ()
{
    return *walletIntegrityVerifier_;
}