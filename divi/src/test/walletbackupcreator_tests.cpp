#include <boost/test/unit_test.hpp>
#include <walletBackupCreator.h>
#include <mockFileSystem.h>
#include <mockFormattedTimestampProvider.h>
#include <iostream>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::_;
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
        EXPECT_CALL(fileSystem,exists(backupDirectoryPath)).WillOnce(Return(false));
        EXPECT_CALL(fileSystem,create_directories(backupDirectoryPath)).Times(1);
        EXPECT_CALL(fileSystem,exists(backupDirectoryPath)).WillOnce(Return(false));

    }
    WalletBackupCreator backupCreator(10, fileSystem, formattedTimestampProvider,  "");

    BOOST_CHECK(!backupCreator.BackupWallet ("/bogusDirectory"));
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
        EXPECT_CALL(fileSystem, exists(backupDirectoryPath));
        EXPECT_CALL(fileSystem, exists(walletPath));
        EXPECT_CALL(fileSystem,copy_file(walletPath,_)).Times(1);
    }

    WalletBackupCreator backupCreator(10, fileSystem, formattedTimestampProvider,  "");
    
    BOOST_CHECK(backupCreator.BackupWallet(dataDirectory));
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
        EXPECT_CALL(fileSystem, exists(backupDirectoryPath));
        EXPECT_CALL(fileSystem, exists(walletPath));
        EXPECT_CALL(fileSystem,copy_file(walletPath,_)).Times(0);
    }

    WalletBackupCreator backupCreator(10, fileSystem, formattedTimestampProvider,  "");
    
    BOOST_CHECK(!backupCreator.BackupWallet(dataDirectory));
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
    }
    WalletBackupCreator backupCreator(10, fileSystem, formattedTimestampProvider,  "");

    backupCreator.BackupWallet(dataDirectory);
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
    WalletBackupCreator backupCreator(1, fileSystem, formattedTimestampProvider,  "");
    
    BOOST_CHECK(backupCreator.BackupWallet(dataDirectory));
}

/*
BOOST_AUTO_TEST_CASE(will_backup_wallet_file)
{
    NiceMock<MockFileSystem> fileSystem;
    MockFormattedTimestampProvider formattedTimestampProvider;
    
    TimeStampedFilePaths namedTimestamps;

    for(int i = 0; i < 11; i++)
    {
        std::string walletDatPath = "/bogusDirectory/backups/wallet.dat" + std::to_string( i );
        
        //fileSystem.addExistsMapping( walletDatPath, true );
        
        namedTimestamps.push_back(std::make_pair( std::time_t(i*10), walletDatPath));
    }
    //fileSystem.addGetTimestampsMapping("/bogusDirectory/backups", namedTimestamps);


    //fileSystem.addExistsMapping("/bogusDirectory/backups", true);
    //fileSystem.addExistsMapping("/bogusDirectory/wallet.dat", true);


    WalletBackupCreator backupCreator(10, fileSystem, formattedTimestampProvider,  "");


    backupCreator.BackupWallet("/bogusDirectory");
    
}
*/
BOOST_AUTO_TEST_SUITE_END()