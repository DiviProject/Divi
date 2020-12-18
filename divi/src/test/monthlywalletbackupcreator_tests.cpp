#include <monthlyWalletBackupCreator.h>
#include <boost/test/unit_test.hpp>
#include <MockFileSystem.h>
#include <string>
#include <ctime>
#include <monthlyWalletBackupCreator.h>
#include <mockWalletBackupCreator.h>
#include <timeIntervalConstants.h>

using ::testing::Exactly;
using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::_;

TimeStampedFolderContents createExpectedTimestampedFolderContents (unsigned int minimumAge, unsigned int maximumAge, unsigned int files = 10) {
    TimeStampedFolderContents expectedTimestamps;

    auto stretchValueBetweenMinAndMax = [](double value, double minimumValue, double maximumValue, double newMinimumValue, double newMaximumValue) -> double
    {
        double originalDifference = maximumValue - minimumValue;
        double newDifference = newMaximumValue - newMinimumValue;

        double scale = newDifference / originalDifference;

        return (value - minimumValue) * scale + newMinimumValue;
    };
    for(unsigned int i = 0; i < files; i++)
    {
        expectedTimestamps.push_back(
            std::pair<std::time_t, std::string> {
                std::time(0) - stretchValueBetweenMinAndMax(i, (double) minimumAge, (double)  maximumAge, 0.0, (double) files),
                std::string("backups") + std::to_string(i)
        });
    }

    std::random_shuffle(expectedTimestamps.begin(),expectedTimestamps.end());

    return expectedTimestamps;
}

BOOST_AUTO_TEST_SUITE(monthly_backup_tests)

BOOST_AUTO_TEST_CASE(monthly_backup_creator_forwards_call)
{

    MockWalletBackupCreator backupCreator;
    NiceMock<MockFileSystem> fileSystem;

    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory + "/monthlyBackups";

    MonthlyWalletBackupCreator monthlyBackupCreator(backupCreator, fileSystem);

    EXPECT_CALL(backupCreator, BackupWallet()).Times(1);

    monthlyBackupCreator.BackupWallet();
}


BOOST_AUTO_TEST_CASE(doesntbackUpWhenEarliestBackupIsUnderAMonthOld)
{
    StrictMock<MockWalletBackupCreator> backupCreator;
    NiceMock<MockFileSystem> fileSystem;

    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory + "/monthlyBackups";

    MonthlyWalletBackupCreator monthlyBackupCreator(backupCreator, fileSystem);

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

    MonthlyWalletBackupCreator monthlyBackupCreator(backupCreator, fileSystem);

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

    MonthlyWalletBackupCreator monthlyBackupCreator(backupCreator, fileSystem);

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
        MonthlyWalletBackupCreator monthlyBackupCreator(backupCreator, fileSystem);

        EXPECT_EQ(monthlyBackupCreator.GetBackupSubfolderDirectory(), backupCreator.GetBackupSubfolderDirectory());
    }

    {
        MockWalletBackupCreator backupCreator;
        MonthlyWalletBackupCreator monthlyBackupCreator(backupCreator, fileSystem);

        ON_CALL(backupCreator, GetBackupSubfolderDirectory()).WillByDefault( Return (backupDirectoryPath) );

        EXPECT_EQ(monthlyBackupCreator.GetBackupSubfolderDirectory(), backupCreator.GetBackupSubfolderDirectory());
    }
}

BOOST_AUTO_TEST_CASE(doesntbackUpWhenTheNewestOfManyFilesIsLessThanAMonthOld)
{
    NiceMock<MockWalletBackupCreator> backupCreator;
    NiceMock<MockFileSystem> fileSystem;

    std::string dataDirectory = "/bogusDirectory";
    std::string backupDirectoryPath = dataDirectory + "/monthlyBackups";

    MonthlyWalletBackupCreator monthlyBackupCreator(backupCreator, fileSystem);

    TimeStampedFolderContents expectedTimestampedFolderContents = createExpectedTimestampedFolderContents (NUMBER_OF_SECONDS_IN_A_MONTH - 1, NUMBER_OF_SECONDS_IN_A_MONTH * 2);

    ON_CALL(backupCreator, GetBackupSubfolderDirectory()).WillByDefault( Return (backupDirectoryPath) );

    ON_CALL(fileSystem, get_timestamped_folder_contents(backupDirectoryPath)).WillByDefault( Return(expectedTimestampedFolderContents) );

    EXPECT_CALL(backupCreator, BackupWallet()).Times(Exactly(0));

    monthlyBackupCreator.BackupWallet();
}

BOOST_AUTO_TEST_SUITE_END()