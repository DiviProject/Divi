#include "walletBackupCreator.h"
#include <utiltime.h>
#include <util.h>
#include <boost/foreach.hpp>
#include <db.h>
#include <iostream>
#include <ui_interface.h>
#include <i_formattedTimestampProvider.h>

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

bool WalletBackupCreator::BackupDatabaseInCaseOfError()
{
    if (!CDB::bitdb.Open(dataDirectory_)) 
    {
        // try moving the database env out of the way
        PathType pathDatabase = dataDirectory_ + "/database";
        PathType pathDatabaseBak = dataDirectory_ + "/database." + std::to_string(GetTime()) + ".bak";
        try {
            fileSystem_.rename(pathDatabase, pathDatabaseBak);
            LogPrintf("Moved old %s to %s. Retrying.\n", pathDatabase.c_str(), pathDatabaseBak.c_str());
        } catch (...) {
            // failure is ok (well, not really, but it's not worse than what we started with)
        }

        // try again
        if (!CDB::bitdb.Open(dataDirectory_)) {
            // if it still fails, it probably means we can't even create the database env
            std::string msg = strprintf(_("Error initializing wallet database environment %s!"), dataDirectory_);
            // return InitError(msg);
            return Error(msg);
        }
    }
    return true;
}

bool WalletBackupCreator::VerifyWallet(std::string strWalletFile)
{
    if (fileSystem_.exists(dataDirectory_ +"/"+ strWalletFile)) {
        CDBEnv::VerifyResult r = CDB::bitdb.Verify(strWalletFile, NULL);

        if (r == CDBEnv::RECOVER_OK) {
            std::string msg = strprintf(_("Warning: wallet.dat corrupt, data salvaged!"
                                        " Original wallet.dat saved as wallet.{timestamp}.bak in %s; if"
                                        " your balance or transactions are incorrect you should"
                                        " restore from a backup."),
                dataDirectory_);
            Warning(msg);
        }
        if (r == CDBEnv::RECOVER_FAIL)
        {
            return Error(_("wallet.dat corrupt, salvage failed"));
        }
    }
    return true;
}

void WalletBackupCreator::ClearFoldersForResync()
{
    // uiInterface.InitMessage(_("Preparing for resync..."));
    // Delete the local blockchain folders to force a resync from scratch to get a consitent blockchain-state
    PathType blocksDir = dataDirectory_ + "/blocks";
    PathType chainstateDir = dataDirectory_ + "/chainstate";
    PathType sporksDir = dataDirectory_ + "/sporks";
    PathType zerocoinDir = dataDirectory_ + "/zerocoin";
    
    LogPrintf("Deleting blockchain folders blocks, chainstate, sporks and zerocoin\n");
    // We delete in 4 individual steps in case one of the folder is missing already
    try {
        if (fileSystem_.exists(blocksDir)){
            fileSystem_.remove_all(blocksDir);
            LogPrintf("-resync: folder deleted: %s\n", blocksDir.c_str());
        }

        if (fileSystem_.exists(chainstateDir)){
            fileSystem_.remove_all(chainstateDir);
            LogPrintf("-resync: folder deleted: %s\n", chainstateDir.c_str());
        }

        if (fileSystem_.exists(sporksDir)){
            fileSystem_.remove_all(sporksDir);
            LogPrintf("-resync: folder deleted: %s\n", sporksDir.c_str());
        }

        if (fileSystem_.exists(zerocoinDir)){
            fileSystem_.remove_all(zerocoinDir);
            LogPrintf("-resync: folder deleted: %s\n", zerocoinDir.c_str());
        }
    } catch (...) {
        LogPrintf("Failed to delete blockchain folders\n");
    }
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
    std::string dateTimeStr = formattedTimestampProvider_.currentTimeStamp();
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

    PathType backupDir = dataDirectory_ + "/backups";
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


bool WalletBackupCreator::CheckWalletIntegrity(bool resync)
{
    if (resync) ClearFoldersForResync();

    LogPrintf("Using wallet %s\n", walletFilename_.c_str());
    // uiInterface.InitMessage(_("Verifying wallet..."));

    if(!BackupDatabaseInCaseOfError()) return false;

    if(!VerifyWallet(walletFilename_)) return false;

    return true;
}