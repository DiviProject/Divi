#include <test_only.h>

#include <UtxoBalanceCalculator.h>
#include <MockUtxoOwnershipDetector.h>
#include <MockSpentOutputTracker.h>
#include <WalletTx.h>

#include <random.h>
#include <RandomTransactionGenerator.h>

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

BOOST_AUTO_TEST_CASE(willIgnoreAllSpentUtxosEvenIfAllAreOwned)
{
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault(Return(true));
    UtxoOwnershipFilter filter(isminetype::ISMINE_SPENDABLE);
    CWalletTx tx = RandomTransactionGenerator()();
    CAmount totalBalance = 0;
    calculator.calculate(tx,GetRandInt(100),filter,totalBalance);
    BOOST_CHECK_EQUAL(totalBalance, 0*COIN);
}

BOOST_AUTO_TEST_SUITE_END()