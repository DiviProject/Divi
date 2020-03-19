#include <monthlyWalletBackupCreator.h>
#include <iostream>
#define NUMBER_OF_SECONDS_IN_A_MONTH 3600 * 24 * 30.
bool MonthlyWalletBackupCreator::BackupWallet()
{
    TimeStampedFolderContents folderContents = fileSystem_.get_timestamped_folder_contents(backupDirectory_);
    if (!folderContents.empty())
    {
        std::pair<std::time_t, PathType> newestFile = folderContents[0];
        std::time_t timestamp = newestFile.first;

        std::time_t currentTime = std::time(0);

        if(std::difftime ( currentTime, timestamp ) >= NUMBER_OF_SECONDS_IN_A_MONTH )
        {
            std::cout << "DiffTime " << std::difftime ( currentTime, timestamp ) << "\n";
            std::cout << "Seconds " << NUMBER_OF_SECONDS_IN_A_MONTH << "\n";
            return backupWalletCreator_.BackupWallet();
        }
    }
    else 
    {
        std::cout << "Size of folder" << folderContents.size() << "\n";
        return backupWalletCreator_.BackupWallet();
    }
    return false;
}

bool MonthlyWalletBackupCreator::CheckWalletIntegrity(bool resync) {
    return true;
}