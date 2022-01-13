#include <test_only.h>
#include <WalletBalanceCalculator.h>
#include <MockTransactionRecord.h>
#include <MockSpentOutputTracker.h>
#include <MockMerkleTxConfirmationNumberCalculator.h>
#include <MockUtxoOwnershipDetector.h>
#include <amount.h>
#include <RandomTransactionGenerator.h>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::_;
using ::testing::Ref;

class WalletBalanceCalculatorTestFixture
{
private:
    MockTransactionRecord::TransactionsByHash transactionsHeldInRecord_;
public:
    NiceMock<MockUtxoOwnershipDetector> utxoOwnershipDetector;
    NiceMock<MockTransactionRecord> transactionRecord;
    NiceMock<MockSpentOutputTracker> spentOutputTracker;
    NiceMock<MockMerkleTxConfirmationNumberCalculator> confsCalculator;
    WalletBalanceCalculator calculator;
    WalletBalanceCalculatorTestFixture(
        ): transactionsHeldInRecord_()
        , utxoOwnershipDetector()
        , transactionRecord()
        , spentOutputTracker()
        , confsCalculator()
        , calculator(utxoOwnershipDetector,transactionRecord,spentOutputTracker,confsCalculator)
    {
        ON_CALL(transactionRecord,GetWalletTransactions()).WillByDefault(ReturnRef(transactionsHeldInRecord_));
    }
};

BOOST_FIXTURE_TEST_SUITE(WalletBalanceCalculatorTests, WalletBalanceCalculatorTestFixture)

BOOST_AUTO_TEST_CASE(theBalanceOfAnEmptyWalletIzZero)
{
    BOOST_CHECK_EQUAL(calculator.getBalance(),CAmount(0));
}

BOOST_AUTO_TEST_CASE(theBalanceOfAWalletMustAccessTheTransactionrecord)
{
    EXPECT_CALL(transactionRecord,GetWalletTransactions()).Times(1);
    BOOST_CHECK_EQUAL(calculator.getBalance(),CAmount(0));
}

BOOST_AUTO_TEST_SUITE_END()