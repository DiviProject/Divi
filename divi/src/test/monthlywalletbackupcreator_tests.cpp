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

    EXPECT_CALL(backupCreator, GetBackupSubfolderDirectory());

    ON_CALL(backupCreator, GetBackupSubfolderDirectory()).WillByDefault( Return (backupDirectoryPath) );

    ON_CALL(fileSystem, get_timestamped_folder_contents(backupDirectoryPath)).WillByDefault( Return(expectedTime) );

    EXPECT_CALL(backupCreator, BackupWallet()).Times(Exactly(0));
    
    monthlyBackupCreator.BackupWallet();
}

BOOST_AUTO_TEST_CASE(doesbackUpWhenEarliestBackupIsOverAMonthOld)
{
    StrictMock<MockWalletBackupCreator> backupCreator;
    NiceMock<MockFileSystem> fileSystem;

    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory + "/monthlyBackups";

    MonthlyWalletBackupCreator monthlyBackupCreator(backupCreator, fileSystem, backupDirectoryPath);
    
    TimeStampedFolderContents expectedTime = {
        std::pair<std::time_t, std::string> {std::time(0) - NUMBER_OF_SECONDS_IN_A_MONTH, "backup1"}
    };

    EXPECT_CALL(backupCreator, GetBackupSubfolderDirectory());

    ON_CALL(backupCreator, GetBackupSubfolderDirectory()).WillByDefault( Return (backupDirectoryPath) );

    ON_CALL(fileSystem, get_timestamped_folder_contents(backupDirectoryPath)).WillByDefault( Return(expectedTime) );

    EXPECT_CALL(backupCreator, BackupWallet());
    
    monthlyBackupCreator.BackupWallet();
}

BOOST_AUTO_TEST_CASE(monthlyBackupCreatorForwardsGetBackupSubfolderDirectoryCall)
{
    StrictMock<MockWalletBackupCreator> backupCreator;
    NiceMock<MockFileSystem> fileSystem;

    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory + "/monthlyBackups";

    MonthlyWalletBackupCreator monthlyBackupCreator(backupCreator, fileSystem, backupDirectoryPath);
    
    EXPECT_CALL(backupCreator, GetBackupSubfolderDirectory());
    
    monthlyBackupCreator.GetBackupSubfolderDirectory();
}

BOOST_AUTO_TEST_CASE(monthlyBackupCreatorForwardedCallReturnsIdenticalOutputs)
{
    NiceMock<MockFileSystem> fileSystem;

    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory + "/monthlyBackups";

    {
        MockWalletBackupCreator backupCreator;
        MonthlyWalletBackupCreator monthlyBackupCreator(backupCreator, fileSystem, backupDirectoryPath);

        EXPECT_EQ(monthlyBackupCreator.GetBackupSubfolderDirectory(), backupCreator.GetBackupSubfolderDirectory());
    }

    {
        MockWalletBackupCreator backupCreator;
        MonthlyWalletBackupCreator monthlyBackupCreator(backupCreator, fileSystem, backupDirectoryPath);

        ON_CALL(backupCreator, GetBackupSubfolderDirectory()).WillByDefault( Return (backupDirectoryPath) );
        
        EXPECT_EQ(monthlyBackupCreator.GetBackupSubfolderDirectory(), backupCreator.GetBackupSubfolderDirectory());
    }
}

BOOST_AUTO_TEST_SUITE_END()