#include <walletBackupFeatureContainer.h>
#include <FilteredBoostFileSystem.h>
#include <walletBackupCreator.h>
#include <monthlyWalletBackupCreator.h>
#include <DatabaseWrapper.h>

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

bool WalletBackupFeatureContainer::backupWallet()
{
    if(walletIntegrityVerifier_->CheckWalletIntegrity(dataDirectory_, walletFileName_))
    {
        return (walletBackupCreator_->BackupWallet() && monthlyWalletBackupDecorator_->BackupWallet());
    }
    return false;
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