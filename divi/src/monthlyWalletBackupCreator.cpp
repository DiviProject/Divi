#include <monthlyWalletBackupCreator.h>
bool MonthlyWalletBackupCreator::BackupWallet()
{
    TimeStampedFolderContents folderContents = fileSystem_.get_timestamped_folder_contents(backupWalletCreator_.GetBackupSubfolderDirectory());
    if (!folderContents.empty())
    {
        std::pair<std::time_t, PathType> newestFile = folderContents[0];
        std::time_t timestamp = newestFile.first;

        std::time_t currentTime = std::time(0);

        if(std::difftime ( currentTime, timestamp ) >= NUMBER_OF_SECONDS_IN_A_MONTH )
        {
            return backupWalletCreator_.BackupWallet();
        }
    }
    else 
    {
        return backupWalletCreator_.BackupWallet();
    }
    return false;
}

bool MonthlyWalletBackupCreator::CheckWalletIntegrity(bool resync) 
{
    return true;
}
std::string MonthlyWalletBackupCreator::GetBackupSubfolderDirectory() const 
{
    return backupWalletCreator_.GetBackupSubfolderDirectory();
}