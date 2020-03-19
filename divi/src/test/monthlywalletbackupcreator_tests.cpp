#include <monthlyWalletBackupCreator.h>
#include <boost/test/unit_test.hpp>
#include <mockFileSystem.h>
#include <mockFormattedTimestampProvider.h>
#include <string>
#include <ctime>
#include <monthlyWalletBackupCreator.h>
#include <mockWalletBackupCreator.h>

using ::testing::Exactly;
using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::_;
BOOST_AUTO_TEST_SUITE(monthly_backup_tests)

BOOST_AUTO_TEST_CASE(monthly_backup_creator_forwards_call)
{

    MockWalletBackupCreator backupCreator;
    NiceMock<MockFileSystem> fileSystem;

    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory + "/monthlyBackups";

    MonthlyWalletBackupCreator monthlyBackupCreator(backupCreator, fileSystem, dataDirectory);
    
    EXPECT_CALL(backupCreator, BackupWallet()).Times(1);
    
    monthlyBackupCreator.BackupWallet();
}


BOOST_AUTO_TEST_CASE(doesntbackUpWhenEarliestBackupIsUnderAMonthOld)
{
    StrictMock<MockWalletBackupCreator> backupCreator;
    NiceMock<MockFileSystem> fileSystem;

    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory + "/monthlyBackups";

    MonthlyWalletBackupCreator monthlyBackupCreator(backupCreator, fileSystem, backupDirectoryPath);
    
    TimeStampedFolderContents expectedTime = {
        std::pair<std::time_t, std::string> {std::time(0), "backup1"}
    };

    bool internalBackupWalletMethodCalled = false;
    ON_CALL(backupCreator, BackupWallet()).WillByDefault(Invoke( [&internalBackupWalletMethodCalled]()->bool { internalBackupWalletMethodCalled = true; return true; } ));
    
    ON_CALL(fileSystem, get_timestamped_folder_contents(backupDirectoryPath)).WillByDefault( Return(expectedTime) );

    // EXPECT_CALL(backupCreator, BackupWallet()).Times(Exactly(0));
    
    monthlyBackupCreator.BackupWallet();

    BOOST_CHECK(!internalBackupWalletMethodCalled);
}

BOOST_AUTO_TEST_SUITE_END()