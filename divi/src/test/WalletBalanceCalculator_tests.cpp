#include <test_only.h>
#include <WalletBalanceCalculator.h>
#include <MockTransactionRecord.h>
#include <MockSpentOutputTracker.h>
#include <MockMerkleTxConfirmationNumberCalculator.h>
#include <MockUtxoOwnershipDetector.h>
#include <amount.h>
#include <RandomTransactionGenerator.h>

using ::testing::NiceMock;
using ::testing::Invoke;
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
    const CWalletTx& getWalletTx(const uint256 txid)
    {
        assert(transactionsHeldInRecord_.count(txid) > 0);
        return transactionsHeldInRecord_[txid];
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
    addTransactionToMockWalletRecord(tx);
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault(Return(false));
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(tx.GetHash()))).WillByDefault(Return(1));
    BOOST_CHECK_EQUAL(calculator.getBalance(),CAmount(tx.GetValueOut()));
}

BOOST_AUTO_TEST_CASE(onlyUnspentUTXOsAddToTheTotalOfBalance)
{
    CTransaction tx = RandomTransactionGenerator()();
    addTransactionToMockWalletRecord(tx);
    const unsigned spentIndex = GetRandInt(tx.vout.size());
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(tx.GetHash()))).WillByDefault(Return(1));
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
    BOOST_CHECK_EQUAL(calculator.getBalance(),expectedBalance);
}

BOOST_AUTO_TEST_CASE(onlyUnspentAndOwnedUTXOsAddToTheTotalOfBalance)
{
    CTransaction tx = RandomTransactionGenerator()();
    addTransactionToMockWalletRecord(tx);
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(tx.GetHash()))).WillByDefault(Return(1));
    const unsigned spentIndex = GetRandInt(tx.vout.size());
    const unsigned notOwnedIndex = GetRandInt(tx.vout.size());

    const uint256 txid = tx.GetHash();
    CAmount expectedBalance = 0;
    for(unsigned outputIndex = 0u; outputIndex < tx.vout.size(); ++outputIndex)
    {
        const bool countsTowardBalance = (outputIndex != spentIndex) && (outputIndex != notOwnedIndex);
        if(outputIndex != spentIndex)
        {
            ON_CALL(spentOutputTracker,IsSpent(txid,outputIndex,_)).WillByDefault(Return(false));
        }
        else
        {
            ON_CALL(spentOutputTracker,IsSpent(txid,outputIndex,_)).WillByDefault(Return(true));
        }

        if(outputIndex != notOwnedIndex)
        {
            ON_CALL(utxoOwnershipDetector,isMine(tx.vout[outputIndex])).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
        }
        else
        {
            ON_CALL(utxoOwnershipDetector,isMine(tx.vout[outputIndex])).WillByDefault(Return(isminetype::ISMINE_NO));
        }
        if(countsTowardBalance) expectedBalance += tx.vout[outputIndex].nValue;
    }
    BOOST_CHECK_EQUAL(calculator.getBalance(),expectedBalance);
}

BOOST_AUTO_TEST_CASE(conflictedSpendsDontAffectTheBalance)
{
    CTransaction tx = RandomTransactionGenerator()(1*COIN,1u,1u);
    addTransactionToMockWalletRecord(tx);
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(tx.GetHash()))).WillByDefault(Return(1));
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault(
        Invoke(
            [](const uint256& hash, unsigned int n, const int minimumConfirmations) -> bool
            {
                return minimumConfirmations < 0;
            }
        )
    );
    BOOST_CHECK_EQUAL(calculator.getBalance(),tx.GetValueOut());
}

BOOST_AUTO_TEST_CASE(conflictedTransactionsDontAffectTheBalance)
{
    CTransaction tx = RandomTransactionGenerator()(1*COIN,1u,1u);
    addTransactionToMockWalletRecord(tx);
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(tx.GetHash()))).WillByDefault(Return(1));
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(tx.GetHash()) ))
        .WillByDefault(Return(-1));

    const CAmount expectedBalance = 0;

    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault( Return(false) );
    BOOST_CHECK_EQUAL(calculator.getBalance(),expectedBalance);
}

BOOST_AUTO_TEST_CASE(unspentUTXOsThatArentOwnedAreIgnored)
{
    CTransaction tx = RandomTransactionGenerator()();
    addTransactionToMockWalletRecord(tx);
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(tx.GetHash()))).WillByDefault(Return(1));
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_NO));
    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault(Return(false));
    BOOST_CHECK_EQUAL(calculator.getBalance(),CAmount(0));
}

BOOST_AUTO_TEST_SUITE_END()