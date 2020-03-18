#include <ctime>
#include <i_walletBackupCreator.h>
#include <i_filesystem.h>



class MonthlyWalletBackupCreator : public I_WalletBackupCreator
{
private:
    I_WalletBackupCreator& backupWalletCreator_;
    I_FileSystem& fileSystem_;
    std::time_t getTimeOfLastBackupDate() const;
    std::string backupDirectory_;
public:
    MonthlyWalletBackupCreator(
        I_WalletBackupCreator& backupWalletCreator,
        I_FileSystem& fileSystem,
        std::string backupDirectory
        ): backupWalletCreator_(backupWalletCreator)
        , fileSystem_(fileSystem)
        , backupDirectory_(backupDirectory) {

        }
    virtual ~MonthlyWalletBackupCreator(){}
    bool BackupWallet()
    {
        
        TimeStampedFolderContents folderContents = fileSystem_.get_timestamped_folder_contents(backupDirectory_);
        bool createBackup = false;
        if(folderContents.size() > 0)
        {
            double secondsInMonth = 2.628e+6;
            std::time_t currentTime = std::time_t(0);
            std::time_t mostRecentBackup = std::time_t(0);
            for(std::vector< std::pair<std::time_t,PathType> >::iterator it = folderContents.begin(); it != folderContents.end(); ++it)
            {
                std::time_t backupTime = (*it).first;
                double secondsSinceLastWriteTime = std::difftime ( currentTime, backupTime );
                if(secondsSinceLastWriteTime > secondsInMonth){
                    createBackup = true;
                }
            }
        }
        else
        {
            createBackup = true;
        }
       
        if(createBackup)
        {
            backupWalletCreator_.BackupWallet();
        }
    }
};
