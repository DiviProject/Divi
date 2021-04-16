#include <monthlyWalletBackupCreator.h>
#include <algorithm>
#include <timeIntervalConstants.h>
#include <utiltime.h>

MonthlyWalletBackupCreator::MonthlyWalletBackupCreator(
    I_WalletBackupCreator& backupWalletCreator,
    I_FileSystem& fileSystem
    ): backupWalletCreator_(backupWalletCreator)
    , fileSystem_(fileSystem)
{
}

bool MonthlyWalletBackupCreator::BackupWallet()
{
    TimeStampedFolderContents folderContents = fileSystem_.get_timestamped_folder_contents(backupWalletCreator_.GetBackupSubfolderDirectory());
    if (!folderContents.empty())
    {
        typedef std::pair<std::time_t, std::string> TimestampedFile;
        std::sort(folderContents.begin(), folderContents.end(), [](const TimestampedFile& a, const TimestampedFile& b){
            return std::difftime(a.first, b.first) < 0.0;
        });

        std::pair<std::time_t, PathType> newestFile = folderContents.back();
        std::time_t newestTimestamp = newestFile.first;

        int64_t timeDifference = GetTime() - static_cast<int64_t>(newestTimestamp);

        if(timeDifference >= NUMBER_OF_SECONDS_IN_A_MONTH )
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

std::string MonthlyWalletBackupCreator::GetBackupSubfolderDirectory() const
{
    return backupWalletCreator_.GetBackupSubfolderDirectory();
}