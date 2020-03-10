#include <boost/test/unit_test.hpp>
#include <walletBackupCreator.h>
#include <mockFileSystem.h>
#include <filesystemtestproxy.h>

BOOST_AUTO_TEST_SUITE(wallet_backups_test)


BOOST_AUTO_TEST_CASE(will_fail_to_backup_to_bogus_directory)
{
    FileSystemTree internalFileSystem;
    MockFileSystem fileSystem(internalFileSystem);

    WalletBackupCreator octal(10, fileSystem, "/");
    BOOST_CHECK(!octal.BackupWallet ("bogusDirectory", false));
}


BOOST_AUTO_TEST_SUITE_END()