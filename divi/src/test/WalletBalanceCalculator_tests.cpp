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
    void addTransactionToMockWalletRecord(const CTransaction& tx)
    {
        transactionsHeldInRecord_[tx.GetHash()] = tx;
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


BOOST_AUTO_TEST_CASE(theBalanceOfAWalletWhoOwnsAllUtxosIsTheTotalOfOutputs)
{
    CTransaction tx = RandomTransactionGenerator()();
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault(Return(false));
    addTransactionToMockWalletRecord(tx);
    BOOST_CHECK_EQUAL(calculator.getBalance(),CAmount(tx.GetValueOut()));
}

BOOST_AUTO_TEST_CASE(onlyUnspentUTXOsAddToTheTotalOfBalance)
{
    CTransaction tx = RandomTransactionGenerator()();
    const unsigned spentIndex = GetRandInt(tx.vout.size());
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
    const uint256 txid = tx.GetHash();
    CAmount expectedBalance = 0;
    for(unsigned outputIndex = 0u; outputIndex < tx.vout.size(); ++outputIndex)
    {
        if(outputIndex != spentIndex)
        {
            ON_CALL(spentOutputTracker,IsSpent(txid,outputIndex,_)).WillByDefault(Return(false));
            expectedBalance += tx.vout[outputIndex].nValue;
        }
        else
        {
            ON_CALL(spentOutputTracker,IsSpent(txid,outputIndex,_)).WillByDefault(Return(true));
        }
    }
    addTransactionToMockWalletRecord(tx);
    BOOST_CHECK_EQUAL(calculator.getBalance(),expectedBalance);
}

BOOST_AUTO_TEST_SUITE_END()