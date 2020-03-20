#include <boost/test/unit_test.hpp>
#include <WalletIntegrityVerifier.h>
#include <mockFileSystem.h>
#include <MockDatabaseWrapper.h>
#include <WalletIntegrityVerifier.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::_;
using ::testing::StrictMock;

BOOST_AUTO_TEST_SUITE(WalletIntegrityVerifierTests)

BOOST_AUTO_TEST_CASE(willFailToVerifyOnNonexistentWalletfile)
{
    NiceMock<MockDatabaseWrapper> dbWrapper;
    NiceMock<MockFileSystem> fileSystem;
    std::string dataDirectory = "/SomeRandomFolder";
    std::string walletFilename = "randomWalletFilename.dat";
    WalletIntegrityVerifier integrityVerifier(fileSystem,dbWrapper);

    BOOST_CHECK(!integrityVerifier.CheckWalletIntegrity(dataDirectory,walletFilename));
}

BOOST_AUTO_TEST_CASE(willCheckDatabaseEnvironmentIsAvailable)
{
    NiceMock<MockFileSystem> fileSystem;
    NiceMock<MockDatabaseWrapper> dbWrapper;
    WalletIntegrityVerifier integrityVerifier(fileSystem,dbWrapper);

    std::string dataDirectory = "/SomeRandomFolder";
    std::string walletFilename = "randomWalletFilename.dat";
    std::string dbFolderPath =  dataDirectory + "/database";

    ON_CALL(dbWrapper, Open(dataDirectory)).WillByDefault(Return(true));
    {
        ::testing::InSequence seq;
        EXPECT_CALL(dbWrapper, Open(dataDirectory)).Times(1);
    }

    EXPECT_EQ(integrityVerifier.CheckWalletIntegrity(dataDirectory,walletFilename),true);
}

BOOST_AUTO_TEST_CASE(willBackupDatabaseIfEnvironmentIsUnavailable)
{
    NiceMock<MockFileSystem> fileSystem;
    NiceMock<MockDatabaseWrapper> dbWrapper;
    WalletIntegrityVerifier integrityVerifier(fileSystem,dbWrapper);

    std::string dataDirectory = "/SomeRandomFolder";
    std::string walletFilename = "randomWalletFilename.dat";
    std::string dbFolderPath =  dataDirectory + "/database";

    ON_CALL(dbWrapper, Open(dataDirectory)).WillByDefault(Return(false));
    {
        ::testing::InSequence seq;
        EXPECT_CALL(dbWrapper, Open(dataDirectory)).Times(1);
        EXPECT_CALL(fileSystem, rename(dbFolderPath,_));
    }

    integrityVerifier.CheckWalletIntegrity(
        dataDirectory,walletFilename);
}

BOOST_AUTO_TEST_SUITE_END()