#include <boost/test/unit_test.hpp>
#include <walletBackupCreator.h>
#include <mockFileSystem.h>

BOOST_AUTO_TEST_SUITE(wallet_backups_test)


BOOST_AUTO_TEST_CASE(will_fail_to_backup_wallet_if_it_cant_create_directory)
{
    MockFileSystem fileSystem;

    WalletBackupCreator backupCreator(10, fileSystem, "/");
    BOOST_CHECK(!backupCreator.BackupWallet ("bogusDirectory", false));
}


BOOST_AUTO_TEST_SUITE_END()