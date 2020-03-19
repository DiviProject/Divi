#include <boost/test/unit_test.hpp>
#include <WalletIntegrityVerifier.h>
#include <mockFileSystem.h>
#include <MockDatabaseWrapper.h>
#include <WalletIntegrityVerifier.h>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::_;

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

BOOST_AUTO_TEST_SUITE_END()