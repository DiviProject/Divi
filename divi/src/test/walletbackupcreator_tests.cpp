#include <boost/test/unit_test.hpp>
#include <walletBackupCreator.h>
#include <mockFileSystem.h>
#include <mockFormattedTimestampProvider.h>
#include <iostream>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::_;
using ::testing::AtLeast;

BOOST_AUTO_TEST_SUITE(wallet_backups_test)


BOOST_AUTO_TEST_CASE(will_fail_to_backup_wallet_if_it_cant_create_directory)
{
    NiceMock<MockFileSystem> fileSystem;
    NiceMock<MockFormattedTimestampProvider> formattedTimestampProvider;

    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory+"/backups";
    ON_CALL(fileSystem, exists(backupDirectoryPath)).WillByDefault(Return(false));
    {
        ::testing::InSequence seq;
        EXPECT_CALL(fileSystem,exists(backupDirectoryPath)).Times(1);
        EXPECT_CALL(fileSystem,create_directories(backupDirectoryPath)).Times(1);
        EXPECT_CALL(fileSystem,exists(backupDirectoryPath)).Times(1);
    }
    WalletBackupCreator backupCreator(10, fileSystem, formattedTimestampProvider,  dataDirectory);

    BOOST_CHECK(!backupCreator.BackupWallet ());
}

BOOST_AUTO_TEST_CASE(will_attempt_backup_to_existing_directory_if_walletfile_exists)
{
    NiceMock<MockFileSystem> fileSystem;
    NiceMock<MockFormattedTimestampProvider> formattedTimestampProvider;
    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory+"/backups";
    std::string walletPath = dataDirectory+"/wallet.dat";

    
    ON_CALL(fileSystem, exists(backupDirectoryPath)).WillByDefault(Return(true));
    ON_CALL(fileSystem, exists(walletPath)).WillByDefault(Return(true));
    {
        ::testing::InSequence seq;
        EXPECT_CALL(fileSystem, exists(backupDirectoryPath)).Times(2);
        EXPECT_CALL(fileSystem, exists(walletPath));
        EXPECT_CALL(fileSystem,copy_file(walletPath,_)).Times(1);
    }

    WalletBackupCreator backupCreator(10, fileSystem, formattedTimestampProvider,  dataDirectory);
    
    BOOST_CHECK(backupCreator.BackupWallet());
}


BOOST_AUTO_TEST_CASE(will_not_backup_to_existing_directory_if_walletfile_does_not_exist)
{
    NiceMock<MockFileSystem> fileSystem;
    NiceMock<MockFormattedTimestampProvider> formattedTimestampProvider;
    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory+"/backups";
    std::string walletPath = dataDirectory+"/wallet.dat";

    
    ON_CALL(fileSystem, exists(backupDirectoryPath)).WillByDefault(Return(true));
    ON_CALL(fileSystem, exists(walletPath)).WillByDefault(Return(false));
    {
        ::testing::InSequence seq;
        EXPECT_CALL(fileSystem, exists(backupDirectoryPath)).Times(2);
        EXPECT_CALL(fileSystem, exists(walletPath));
        EXPECT_CALL(fileSystem,copy_file(walletPath,_)).Times(0);
    }

    WalletBackupCreator backupCreator(10, fileSystem, formattedTimestampProvider,  dataDirectory);
    
    BOOST_CHECK(!backupCreator.BackupWallet());
}


BOOST_AUTO_TEST_CASE(will_create_backup_directory_if_nonexistent)
{
    NiceMock<MockFileSystem> fileSystem;
    NiceMock<MockFormattedTimestampProvider> formattedTimestampProvider;
    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory+"/backups";

    
    ON_CALL(fileSystem, exists(backupDirectoryPath))
        .WillByDefault(
            Return(false));
    {
        ::testing::InSequence seq;
        EXPECT_CALL(fileSystem, exists(backupDirectoryPath));
        EXPECT_CALL(fileSystem,create_directories(backupDirectoryPath)).Times(1);
        EXPECT_CALL(fileSystem, exists(backupDirectoryPath));
    }
    WalletBackupCreator backupCreator(10, fileSystem, formattedTimestampProvider,  dataDirectory);

    backupCreator.BackupWallet();
}


BOOST_AUTO_TEST_CASE(will_create_backup_file)
{
    NiceMock<MockFileSystem> fileSystem;
    NiceMock<MockFormattedTimestampProvider> formattedTimestampProvider;

    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory+"/backups";
    std::string walletPath = dataDirectory+"/wallet.dat";
    std::string backupfileSuffix = "backup.1-1-2020";

    ON_CALL(fileSystem,exists(backupDirectoryPath)).WillByDefault(Return(true));
    ON_CALL(fileSystem,exists(walletPath)).WillByDefault(Return(true));
    ON_CALL(formattedTimestampProvider, currentTimeStamp()).WillByDefault(Return(backupfileSuffix));

    EXPECT_CALL(fileSystem, copy_file( walletPath, backupDirectoryPath +"/wallet.dat"+ backupfileSuffix )).Times(1);
    WalletBackupCreator backupCreator(1, fileSystem, formattedTimestampProvider,  dataDirectory);
    
    BOOST_CHECK(backupCreator.BackupWallet());
}

TimeStampedFolderContents createTimestampedFolderContents(const std::string& directoryPath, unsigned numberOfFiles)
{
    TimeStampedFolderContents timestampedContents;
    for(unsigned fileindex = 0u; fileindex < numberOfFiles; ++fileindex)
    {
        unsigned timestamp = 45u*fileindex;
        std::string filepath = directoryPath + "/wallet.dat" +"/" + std::to_string(timestamp);
        timestampedContents.push_back(std::pair<std::time_t,std::string>{std::time_t(timestamp),filepath});
    }
    return timestampedContents;
}

BOOST_AUTO_TEST_CASE(will_remove_no_backups_when_at_or_below_maximum_number_of_backups)
{
    NiceMock<MockFileSystem> fileSystem;
    NiceMock<MockFormattedTimestampProvider> formattedTimestampProvider;
    
    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory+"/backups";
    std::string walletPath = dataDirectory+"/wallet.dat";

    ON_CALL(fileSystem, exists(backupDirectoryPath)).WillByDefault(Return(true));
    ON_CALL(fileSystem, exists(walletPath)).WillByDefault(Return(true));

    unsigned numberOfBackups = 10u;
    ON_CALL(fileSystem, get_timestamped_folder_contents(backupDirectoryPath))
        .WillByDefault(Return(createTimestampedFolderContents(backupDirectoryPath,numberOfBackups)));
    EXPECT_CALL(fileSystem, remove(_)).Times(0);

    WalletBackupCreator backupCreator(numberOfBackups, fileSystem, formattedTimestampProvider,  dataDirectory);

    backupCreator.BackupWallet();
}

BOOST_AUTO_TEST_CASE(will_remove_files_down_to_maximum_number_of_backups)
{
    NiceMock<MockFileSystem> fileSystem;
    NiceMock<MockFormattedTimestampProvider> formattedTimestampProvider;
    
    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory+"/backups";
    std::string walletPath = dataDirectory+"/wallet.dat";

    ON_CALL(fileSystem, exists(backupDirectoryPath)).WillByDefault(Return(true));
    ON_CALL(fileSystem, exists(walletPath)).WillByDefault(Return(true));

    unsigned maximumNumberOfBackups = 10u;
    unsigned numberOfExcessBackups = 27u;
    unsigned numberOfFilesInsideBackupFolder = maximumNumberOfBackups + numberOfExcessBackups;
    ON_CALL(fileSystem, get_timestamped_folder_contents(backupDirectoryPath))
        .WillByDefault(Return(createTimestampedFolderContents(backupDirectoryPath,numberOfFilesInsideBackupFolder)));
    EXPECT_CALL(fileSystem, remove(_)).Times(numberOfExcessBackups);

    WalletBackupCreator backupCreator(maximumNumberOfBackups, fileSystem, formattedTimestampProvider,  dataDirectory);

    backupCreator.BackupWallet();
}

BOOST_AUTO_TEST_SUITE_END()