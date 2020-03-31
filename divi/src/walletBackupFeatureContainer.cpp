#include <walletBackupFeatureContainer.h>
#include <FilteredBoostFileSystem.h>
#include <walletBackupCreator.h>
#include <monthlyWalletBackupCreator.h>
#include <DatabaseWrapper.h>

WalletBackupFeatureContainer::WalletBackupFeatureContainer (
        int numberOfBackups,
        std::string walletFileName,
        std::string dataDirectory
    ): fileSystem_(std::make_unique<FilteredBoostFileSystem>(walletFileName))
    , walletBackupCreator_(std::make_unique<WalletBackupCreator>(numberOfBackups, *fileSystem_, dataDirectory, walletFileName, "/backups"))
    , monthlyWalletBackupCreator_(std::make_unique<WalletBackupCreator>(numberOfBackups, *fileSystem_, dataDirectory, walletFileName, "/monthlyBackups"))
    , monthlyWalletBackupDecorator_(std::make_unique<MonthlyWalletBackupCreator>(*monthlyWalletBackupCreator_, *fileSystem_))
    , database_(std::make_unique<DatabaseWrapper>())
    , walletIntegrityVerifier_(std::make_unique<WalletIntegrityVerifier>(*fileSystem_, *database_))
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