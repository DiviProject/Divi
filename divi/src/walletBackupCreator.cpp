#include "walletBackupCreator.h"
#include <utiltime.h>
#include <util.h>
#include <boost/foreach.hpp>
#include <db.h>

bool Error (std::string msg) {
    LogPrintf("%s", msg);
    return false;
};

bool Warning (std::string msg) {
    LogPrintf("%s", msg);
    return true;
};

bool WalletBackupCreator::BackupDatabase()
{
    std::string strDataDir = GetDataDir().string();
    if (!bitdb.Open(strDataDir)) 
    {
        // try moving the database env out of the way
        boost::filesystem::path pathDatabase = GetDataDir() / "database";
        boost::filesystem::path pathDatabaseBak = GetDataDir() / strprintf("database.%d.bak", GetTime());
        try {
            boost::filesystem::rename(pathDatabase, pathDatabaseBak);
            LogPrintf("Moved old %s to %s. Retrying.\n", pathDatabase.string(), pathDatabaseBak.string());
        } catch (boost::filesystem::filesystem_error& error) {
            // failure is ok (well, not really, but it's not worse than what we started with)
        }

        // try again
        if (!bitdb.Open(GetDataDir())) {
            // if it still fails, it probably means we can't even create the database env
            std::string msg = strprintf(("Error initializing wallet database environment %s!"), strDataDir);
            // return InitError(msg);
            return Error(msg);
        }
    }
    return true;
}

bool WalletBackupCreator::VerifyWallet(std::string strWalletFile)
{
    std::string strDataDir = GetDataDir().string();
    if (boost::filesystem::exists(GetDataDir() / strWalletFile)) {
        CDBEnv::VerifyResult r = bitdb.Verify(strWalletFile, NULL);
        if (r == CDBEnv::RECOVER_OK) {
            std::string msg = strprintf(("Warning: wallet.dat corrupt, data salvaged!"
                                        " Original wallet.dat saved as wallet.{timestamp}.bak in %s; if"
                                        " your balance or transactions are incorrect you should"
                                        " restore from a backup."),
                strDataDir);
            // InitWarning(msg);
            Warning(msg);
        }
        if (r == CDBEnv::RECOVER_FAIL)
        
            // return InitError(_("wallet.dat corrupt, salvage failed"));
            return Error(("wallet.dat corrupt, salvage failed"));
    }
    return true;
}

void WalletBackupCreator::ClearFoldersForResync()
{
    // uiInterface.InitMessage(_("Preparing for resync..."));
    // Delete the local blockchain folders to force a resync from scratch to get a consitent blockchain-state
    boost::filesystem::path blocksDir = GetDataDir() / "blocks";
    boost::filesystem::path chainstateDir = GetDataDir() / "chainstate";
    boost::filesystem::path sporksDir = GetDataDir() / "sporks";
    boost::filesystem::path zerocoinDir = GetDataDir() / "zerocoin";
    
    LogPrintf("Deleting blockchain folders blocks, chainstate, sporks and zerocoin\n");
    // We delete in 4 individual steps in case one of the folder is missing already
    try {
        if (boost::filesystem::exists(blocksDir)){
            boost::filesystem::remove_all(blocksDir);
            LogPrintf("-resync: folder deleted: %s\n", blocksDir.string().c_str());
        }

        if (boost::filesystem::exists(chainstateDir)){
            boost::filesystem::remove_all(chainstateDir);
            LogPrintf("-resync: folder deleted: %s\n", chainstateDir.string().c_str());
        }
        
        if (boost::filesystem::exists(sporksDir)){
            boost::filesystem::remove_all(sporksDir);
            LogPrintf("-resync: folder deleted: %s\n", sporksDir.string().c_str());
        }

        if (boost::filesystem::exists(zerocoinDir)){
            boost::filesystem::remove_all(zerocoinDir);
            LogPrintf("-resync: folder deleted: %s\n", zerocoinDir.string().c_str());
        }
    } catch (boost::filesystem::filesystem_error& error) {
        LogPrintf("Failed to delete blockchain folders %s\n", error.what());
    }
}

void WalletBackupCreator::BackupFile(boost::filesystem::path& sourceFile,boost::filesystem::path& backupFile)
{
#if BOOST_VERSION >= 158000
    try {
        boost::filesystem::copy_file(sourceFile, backupFile);
        LogPrintf("Creating backup of %s -> %s\n", sourceFile, backupFile);
    } catch (boost::filesystem::filesystem_error& error) {
        LogPrintf("Failed to create backup %s\n", error.what());
    }
#else
    std::ifstream src(sourceFile.string(), std::ios::binary);
    std::ofstream dst(backupFile.string(), std::ios::binary);
    dst << src.rdbuf();
#endif
}

void WalletBackupCreator::BackupWalletFile(std::string strWalletFile, boost::filesystem::path backupDir)
{
    // Create backup of the wallet
    std::string dateTimeStr = DateTimeStrFormat(".%Y-%m-%d-%H-%M", GetTime());
    std::string backupPathStr = backupDir.string();
    backupPathStr += "/" + strWalletFile;
    std::string sourcePathStr = GetDataDir().string();
    sourcePathStr += "/" + strWalletFile;
    boost::filesystem::path sourceFile = sourcePathStr;
    boost::filesystem::path backupFile = backupPathStr + dateTimeStr;
    sourceFile.make_preferred();
    backupFile.make_preferred();
    if (boost::filesystem::exists(sourceFile)) {
        BackupFile(sourceFile,backupFile);
    }
}

void WalletBackupCreator::PruneOldBackups(std::string strWalletFile, boost::filesystem::path backupDir)
{
    // Keep only the last 10 backups, including the new one of course
    typedef std::multimap<std::time_t, boost::filesystem::path> folder_set_t;
    folder_set_t folder_set;
    boost::filesystem::directory_iterator end_iter;
    boost::filesystem::path backupFolder = backupDir.string();
    backupFolder.make_preferred();
    // Build map of backup files for current(!) wallet sorted by last write time
    boost::filesystem::path currentFile;
    for (boost::filesystem::directory_iterator dir_iter(backupFolder); dir_iter != end_iter; ++dir_iter) {
        // Only check regular files
        if (boost::filesystem::is_regular_file(dir_iter->status())) {
            currentFile = dir_iter->path().filename();
            // Only add the backups for the current wallet, e.g. wallet.dat.*
            if (dir_iter->path().stem().string() == strWalletFile) {
                folder_set.insert(folder_set_t::value_type(boost::filesystem::last_write_time(dir_iter->path()), *dir_iter));
            }
        }
    }
    // Loop backward through backup files and keep the N newest ones (1 <= N <= 10)
    int counter = 0;
    typedef std::pair<const std::time_t, boost::filesystem::path> TimeStampFilePathPair;
    BOOST_REVERSE_FOREACH (TimeStampFilePathPair file, folder_set) {
        counter++;
        if (counter > nWalletBackups) {
            // More than nWalletBackups backups: delete oldest one(s)
            try {
                boost::filesystem::remove(file.second);
                LogPrintf("Old backup deleted: %s\n", file.second);
            } catch (boost::filesystem::filesystem_error& error) {
                LogPrintf("Failed to delete backup %s\n", error.what());
            }
        }
    }
}

bool WalletBackupCreator::BackupWallet(std::string strDataDir, bool fDisableWallet)
{
#ifdef ENABLE_WALLET
    std::string strWalletFile = GetArg("-wallet", "wallet.dat");
    if (!fDisableWallet) {
        boost::filesystem::path backupDir = GetDataDir() / "backups";
        if (!boost::filesystem::exists(backupDir)) {
            // Always create backup folder to not confuse the operating system's file browser
            boost::filesystem::create_directories(backupDir);
        }
        nWalletBackups = GetArg("-createwalletbackups", 10);
        nWalletBackups = std::max(0, std::min(10, nWalletBackups));
        if (nWalletBackups > 0) {
            if (boost::filesystem::exists(backupDir)) 
            {
                BackupWalletFile(strWalletFile,backupDir);
                PruneOldBackups(strWalletFile,backupDir);
            }
        }

        if (GetBoolArg("-resync", false)) ClearFoldersForResync();

        LogPrintf("Using wallet %s\n", strWalletFile);
        // uiInterface.InitMessage(_("Verifying wallet..."));

        if(!BackupDatabase()) return false;

        if(!VerifyWallet(strWalletFile)) return false;

    }  
#endif // ENABLE_WALLET
    return true;
}