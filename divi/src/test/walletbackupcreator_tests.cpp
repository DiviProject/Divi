#include <boost/test/unit_test.hpp>
#include <walletBackupCreator.h>


BOOST_AUTO_TEST_SUITE(wallet_backups_test)


BOOST_AUTO_TEST_CASE(will_fail_to_backup_to_bogus_directory)
{
    WalletBackupCreator Octal(1);
    BOOST_CHECK(!Octal.BackupWallet ("bogusDirectory", false));
    
}


BOOST_AUTO_TEST_SUITE_END()