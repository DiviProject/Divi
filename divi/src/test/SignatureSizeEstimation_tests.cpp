#include <test_only.h>

#include <key.h>
#include <pubkey.h>
#include <primitives/transaction.h>
#include <keystore.h>
#include <SignatureSizeEstimator.h>
#include <destination.h>

class SignatureSizeTestFixture
{
public:
    SignatureSizeTestFixture(){}
};

BOOST_FIXTURE_TEST_SUITE(SignatureSizeEstimationTests, SignatureSizeTestFixture)

BOOST_AUTO_TEST_CASE(willDefaultToALargeByteSizeWhenScriptIsEmpty)
{
    CBasicKeyStore emptyKeyStore;
    BOOST_CHECK_EQUAL(
        SignatureSizeEstimator::MaxBytesNeededForSigning(emptyKeyStore,CScript()),
        std::numeric_limits<unsigned>::max());
}

BOOST_AUTO_TEST_SUITE_END()