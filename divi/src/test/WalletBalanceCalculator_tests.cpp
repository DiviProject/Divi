#include <test_only.h>
#include <WalletBalanceCalculator.h>
#include <MockTransactionRecord.h>
#include <MockSpentOutputTracker.h>
#include <MockMerkleTxConfirmationNumberCalculator.h>
class WalletBalanceCalculatorTestFixture
{
public:
    MockTransactionRecord transactionRecord;
    MockSpentOutputTracker spentOutputTracker;
    MockMerkleTxConfirmationNumberCalculator confsCalculator;
    WalletBalanceCalculator calculator;
    WalletBalanceCalculatorTestFixture(
        ): transactionRecord()
        , spentOutputTracker()
        , confsCalculator()
        , calculator(transactionRecord,spentOutputTracker,confsCalculator)
    {
    }
};

BOOST_FIXTURE_TEST_SUITE(WalletBalanceCalculatorTests, WalletBalanceCalculatorTestFixture)

BOOST_AUTO_TEST_CASE(EmptyTest)
{

}

BOOST_AUTO_TEST_SUITE_END()