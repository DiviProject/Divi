#include <boost/test/unit_test.hpp>
#include <walletBackupCreator.h>
#include <mockFileSystem.h>
#include <mockFormattedTimestampProvider.h>
#include <iostream>

BOOST_AUTO_TEST_SUITE(wallet_backups_test)


BOOST_AUTO_TEST_CASE(will_fail_to_backup_wallet_if_it_cant_create_directory)
{
    MockFileSystem fileSystem;
    MockFormattedTimestampProvider formattedTimestampProvider;

    WalletBackupCreator backupCreator(10, fileSystem, formattedTimestampProvider,  "");
    BOOST_CHECK(!backupCreator.BackupWallet ("/bogusDirectory"));
}

BOOST_AUTO_TEST_CASE(will_attempt_backup_to_existing_directory)
{
    MockFileSystem fileSystem;
    MockFormattedTimestampProvider formattedTimestampProvider;

    fileSystem.addExistsMapping("/bogusDirectory/backups", true);

    BOOST_CHECK(fileSystem.exists("/bogusDirectory/backups"));

    WalletBackupCreator backupCreator(10, fileSystem, formattedTimestampProvider,  "");
    
    BOOST_CHECK(backupCreator.BackupWallet("/bogusDirectory"));
}


BOOST_AUTO_TEST_CASE(will_create_backup_file)
{
    MockFileSystem fileSystem;
    MockFormattedTimestampProvider formattedTimestampProvider;

    fileSystem.addExistsMapping("/bogusDirectory/backups", true);
    fileSystem.addExistsMapping("/bogusDirectory/wallet.dat", true);
    formattedTimestampProvider.addCurrentTimestampMapping("backup.1-1-2020");

    WalletBackupCreator backupCreator(1, fileSystem, formattedTimestampProvider,  "");
    
    BOOST_CHECK(backupCreator.BackupWallet("/bogusDirectory"));

    BOOST_CHECK(fileSystem.exists("/bogusDirectory/backups/wallet.datbackup.1-1-2020"));
}

BOOST_AUTO_TEST_CASE(will_create_backup_file_2)
{
    MockFileSystem fileSystem;
    MockFormattedTimestampProvider formattedTimestampProvider;

    fileSystem.addExistsMapping("/bogusDirectory/backups", true);
    fileSystem.addExistsMapping("/bogusDirectory/wallet.dat", true);

    WalletBackupCreator backupCreator(1, fileSystem, formattedTimestampProvider,  "");
    
    BOOST_CHECK(backupCreator.BackupWallet("/bogusDirectory"));

    BOOST_CHECK(fileSystem.exists("/bogusDirectory/backups/wallet.dat0"));

}

BOOST_AUTO_TEST_CASE(will_backup_wallet_file)
{
    MockFileSystem fileSystem;
    MockFormattedTimestampProvider formattedTimestampProvider;
    
    TimeStampedFilePaths namedTimestamps;

    for(int i = 0; i < 11; i++)
    {
        std::string walletDatPath = "/bogusDirectory/backups/wallet.dat" + std::to_string( i );
        
        fileSystem.addExistsMapping( walletDatPath, true );
        
        namedTimestamps.push_back(std::make_pair( std::time_t(i*10), walletDatPath));
    }
    fileSystem.addGetTimestampsMapping("/bogusDirectory/backups", namedTimestamps);


    fileSystem.addExistsMapping("/bogusDirectory/backups", true);
    fileSystem.addExistsMapping("/bogusDirectory/wallet.dat", true);


    WalletBackupCreator backupCreator(10, fileSystem, formattedTimestampProvider,  "");


    backupCreator.BackupWallet("/bogusDirectory");
    
}

BOOST_AUTO_TEST_SUITE_END()