#include <test_only.h>

#include <UtxoBalanceCalculator.h>
#include <MockUtxoOwnershipDetector.h>
#include <MockSpentOutputTracker.h>
#include <WalletTx.h>

using ::testing::NiceMock;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::_;
using ::testing::Ref;

class UtxoBalanceCalculatorTestFixture
{
public:
    MockUtxoOwnershipDetector utxoOwnershipDetector;
    MockSpentOutputTracker spentOutputTracker;
    UtxoBalanceCalculator calculator;
    UtxoBalanceCalculatorTestFixture(
        ): utxoOwnershipDetector()
        , spentOutputTracker()
        , calculator(utxoOwnershipDetector,spentOutputTracker)
    {
    }
};

BOOST_FIXTURE_TEST_SUITE(UtxoBalanceCalculatorTests, UtxoBalanceCalculatorTestFixture)

BOOST_AUTO_TEST_CASE(testFixtureWorks)
{
}

BOOST_AUTO_TEST_SUITE_END()