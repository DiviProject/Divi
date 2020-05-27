#include "walletBackupCreator.h"
#include <utiltime.h>
#include <util.h>
#include <boost/foreach.hpp>
#include <db.h>
#include <iostream>
#include <ui_interface.h>

WalletBackupCreator::WalletBackupCreator (
    int numberOfBackups,
    I_FileSystem& fileSystem,
    std::string dataDirectory,
    std::string walletFilename,
    std::string backupSubfolderDirectory
    ): nWalletBackups(numberOfBackups)
    , fileSystem_(fileSystem)
    , dataDirectory_(dataDirectory)
    , walletFilename_(walletFilename)
    , backupSubfolderDirectory_(backupSubfolderDirectory)
{
    nWalletBackups = std::max(0, nWalletBackups);
}

bool Error (std::string msg) 
{
    LogPrintf("%s", msg);
    return false;
}

bool Warning (std::string msg) 
{
    LogPrintf("%s", msg);
    return true;
}

std::string make_preferred(std::string path)
{
    boost::filesystem::path boostPath = path;
    boostPath.make_preferred();
    return boostPath.string();
}

bool WalletBackupCreator::BackupFile(PathType& sourceFile, PathType& backupFile)
{
    try {
        fileSystem_.copy_file(sourceFile, backupFile);
        LogPrintf("Creating backup of %s -> %s\n", sourceFile.c_str(), backupFile.c_str());
        return true;
    } catch (...) {
        LogPrintf("Failed to create backup\n");
        return false;
    }
    return false;
}

bool WalletBackupCreator::BackupWalletFile(std::string strWalletFile, PathType backupDir)
{
    // Create backup of the wallet
    std::string dateTimeStr = DateTimeStrFormat(".%Y-%m-%d-%H-%M", GetTime());
    std::string backupPathStr = backupDir;
    backupPathStr += "/" + strWalletFile;
    std::string sourcePathStr = dataDirectory_;
    sourcePathStr += "/" + strWalletFile;
    PathType sourceFile = sourcePathStr;
    PathType backupFile = backupPathStr + dateTimeStr;
    sourceFile = make_preferred(sourceFile);
    backupFile = make_preferred(backupFile);
    if (fileSystem_.exists(sourceFile)) {
        return BackupFile(sourceFile,backupFile);
    }
    return false;
}

void WalletBackupCreator::PruneOldBackups(std::string strWalletFile, PathType backupDir)
{
    // Loop backward through backup files and keep the N newest ones (1 <= N <= 10)
    TimeStampedFolderContents folderContents = fileSystem_.get_timestamped_folder_contents(backupDir);
    if(folderContents.empty()){
        return;
    }
    typedef TimeStampedFolderContents::value_type TimeStampedPath;
    std::sort(folderContents.begin(), folderContents.end(), 
        [](const TimeStampedPath& a, const TimeStampedPath& b) 
        {
            return a.first < b.first;
        });
    
    int counter = 0;
    BOOST_REVERSE_FOREACH (TimeStampedFolderContents::value_type file, folderContents) {
        counter++;
        if (counter > nWalletBackups) {
            // More than nWalletBackups backups: delete oldest one(s)
            try {
                fileSystem_.remove(file.second);
                LogPrintf("Old backup deleted: %s\n", file.second.c_str());
            } catch (...) {
                LogPrintf("Failed to delete backup\n");
            }
        }
    }
}

bool WalletBackupCreator::BackupWallet()
{
    bool backupWalletStatus = false;

    PathType backupDir = dataDirectory_ + backupSubfolderDirectory_;
    if (!fileSystem_.exists(backupDir)) {
        // Always create backup folder to not confuse the operating system's file browser
        fileSystem_.create_directories(backupDir);
    }

    if (nWalletBackups > 0) {
        if (fileSystem_.exists(backupDir))
        {
            backupWalletStatus = BackupWalletFile(walletFilename_,backupDir);
            PruneOldBackups(walletFilename_,backupDir);
        }
    }

    return backupWalletStatus;
}

std::string WalletBackupCreator::GetBackupSubfolderDirectory() const
{
    return dataDirectory_ + backupSubfolderDirectory_;
}