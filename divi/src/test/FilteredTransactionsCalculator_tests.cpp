#include <test_only.h>
#include <WalletBalanceCalculator.h>
#include <MockTransactionRecord.h>
#include <MockMerkleTxConfirmationNumberCalculator.h>
#include <MockUtxoBalanceCalculator.h>

#include <amount.h>
#include <RandomTransactionGenerator.h>

using ::testing::NiceMock;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::_;
using ::testing::Ref;

class FilteredTransactionsCalculatorTestFixture
{
public:
    MockTransactionRecord txRecord;
    MockMerkleTxConfirmationNumberCalculator confsCalculator;
    MockUtxoBalanceCalculator utxoBalanceCalculator;
    FilteredTransactionsCalculator<CAmount> txBalancesCalculator;
    FilteredTransactionsCalculatorTestFixture(
        ): txRecord()
        , confsCalculator()
        , utxoBalanceCalculator()
        , txBalancesCalculator(txRecord,confsCalculator,utxoBalanceCalculator)
    {
    }
};

BOOST_FIXTURE_TEST_SUITE(FilteredTransactionCalculatorTests, FilteredTransactionsCalculatorTestFixture)

BOOST_AUTO_TEST_CASE(testFixtureWorks)
{
}

BOOST_AUTO_TEST_SUITE_END()