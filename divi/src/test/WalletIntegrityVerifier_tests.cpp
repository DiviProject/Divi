#include <boost/test/unit_test.hpp>
#include <WalletIntegrityVerifier.h>
#include <mockFileSystem.h>
#include <MockDatabaseWrapper.h>
#include <WalletIntegrityVerifier.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <set>

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


BOOST_AUTO_TEST_CASE(willGracefullyFailOnFilesystemError)
{
    NiceMock<MockFileSystem> fileSystem;
    NiceMock<MockDatabaseWrapper> dbWrapper;
    WalletIntegrityVerifier integrityVerifier(fileSystem,dbWrapper);

    std::string dataDirectory = "/SomeRandomFolder";
    std::string walletFilename = "randomWalletFilename.dat";
    

    ON_CALL(dbWrapper, Open(dataDirectory)).WillByDefault(Return(false));
    ON_CALL(fileSystem, rename(_,_))
        .WillByDefault(::testing::Throw(std::runtime_error("Failed to copy folder")));
  
    BOOST_CHECK(!integrityVerifier.CheckWalletIntegrity(
        dataDirectory,walletFilename));
}

BOOST_AUTO_TEST_CASE(willBackupToADifferentFolderEachTime)
{
    NiceMock<MockFileSystem> fileSystem;
    NiceMock<MockDatabaseWrapper> dbWrapper;
    WalletIntegrityVerifier integrityVerifier(fileSystem,dbWrapper);

    std::string dataDirectory = "/SomeRandomFolder";
    std::string walletFilename = "randomWalletFilename.dat";
    

    std::set<std::string> usedDbBackupFolderPath;
    ON_CALL(dbWrapper, Open(dataDirectory)).WillByDefault(Return(false));
    ON_CALL(fileSystem, rename(_,_))
        .WillByDefault( 
            Invoke( [&usedDbBackupFolderPath](const PathType& a, const PathType b)
            {
                usedDbBackupFolderPath.insert(b);
            })
        );

    BOOST_CHECK(!integrityVerifier.CheckWalletIntegrity(
        dataDirectory,walletFilename));
    BOOST_CHECK(!integrityVerifier.CheckWalletIntegrity(
        dataDirectory,walletFilename));
    BOOST_CHECK(usedDbBackupFolderPath.size()==2);
}


BOOST_AUTO_TEST_CASE(willOnlyCheckWalletIntegrityIfDatabaseIsUnavailable)
{
    {
        NiceMock<MockFileSystem> fileSystem;
        NiceMock<MockDatabaseWrapper> dbWrapper;
        WalletIntegrityVerifier integrityVerifier(fileSystem,dbWrapper);

        std::string dataDirectory = "/SomeRandomFolder";
        std::string walletFilename = "randomWalletFilename.dat";
        

        ON_CALL(dbWrapper, Open(dataDirectory)).WillByDefault(Return(false));
        EXPECT_CALL(dbWrapper, Verify(_)).Times(0);

        integrityVerifier.CheckWalletIntegrity(
            dataDirectory,walletFilename);
    }

    {
        NiceMock<MockFileSystem> fileSystem;
        NiceMock<MockDatabaseWrapper> dbWrapper;
        WalletIntegrityVerifier integrityVerifier(fileSystem,dbWrapper);

        std::string dataDirectory = "/SomeRandomFolder";
        std::string walletFilename = "randomWalletFilename.dat";
        

        ON_CALL(dbWrapper, Open(dataDirectory)).WillByDefault(Return(true));
        EXPECT_CALL(dbWrapper, Verify(_)).Times(1);

        integrityVerifier.CheckWalletIntegrity(
            dataDirectory,walletFilename);
    }
}

BOOST_AUTO_TEST_CASE(willOnlyAttemptToVerifyWalletIfFileExists)
{
    {
        NiceMock<MockFileSystem> fileSystem;
        NiceMock<MockDatabaseWrapper> dbWrapper;
        WalletIntegrityVerifier integrityVerifier(fileSystem,dbWrapper);

        std::string dataDirectory = "/SomeRandomFolder";
        std::string walletFilename = "randomWalletFilename.dat";

        ON_CALL(dbWrapper, Open(dataDirectory))
            .WillByDefault(Return(true));
        ON_CALL(fileSystem, exists(dataDirectory+"/"+walletFilename))
            .WillByDefault(Return(false));
        EXPECT_CALL(dbWrapper, Verify(_)).Times(0);

        integrityVerifier.CheckWalletIntegrity(
            dataDirectory,walletFilename);
    }
    {
        NiceMock<MockFileSystem> fileSystem;
        NiceMock<MockDatabaseWrapper> dbWrapper;
        WalletIntegrityVerifier integrityVerifier(fileSystem,dbWrapper);

        std::string dataDirectory = "/SomeRandomFolder";
        std::string walletFilename = "randomWalletFilename.dat";
        

        ON_CALL(dbWrapper, Open(dataDirectory))
            .WillByDefault(Return(true));
        ON_CALL(fileSystem, exists(dataDirectory+"/"+walletFilename))
            .WillByDefault(Return(true));
        EXPECT_CALL(dbWrapper, Verify(_)).Times(1);

        integrityVerifier.CheckWalletIntegrity(
            dataDirectory,walletFilename);
    }
}

BOOST_AUTO_TEST_SUITE_END()