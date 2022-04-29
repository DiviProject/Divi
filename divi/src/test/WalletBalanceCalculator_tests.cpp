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
    CTransaction createRandomSpendingTransaction(const CTransaction& txToSpendFrom, unsigned minimumNumberOfOutputs=1u)
    {
        assert(txToSpendFrom.vout.size() > 0u);
        const unsigned maxUtxoCountToSpendFrom = GetRandInt(txToSpendFrom.vout.size()) + 1;
        CMutableTransaction spendingTxTemplate = RandomTransactionGenerator()(0*COIN, maxUtxoCountToSpendFrom,minimumNumberOfOutputs);
        CAmount totalInputSpent = 0;
        for(CTxIn& input: spendingTxTemplate.vin)
        {
            input.prevout.hash = txToSpendFrom.GetHash();
            totalInputSpent += txToSpendFrom.vout[input.prevout.n].nValue;
        }
        for(CTxOut& output: spendingTxTemplate.vout)
        {
            output.nValue = GetRand(std::max(totalInputSpent,CAmount(0) ));
            totalInputSpent -= output.nValue;
            assert(output.nValue >=0);
        }
        CTransaction spendingTransaction = spendingTxTemplate;
        return spendingTransaction;
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

BOOST_AUTO_TEST_CASE(willIgnoreUnconfirmedTransactionsNonDebitingTransaction)
{
    CTransaction tx = RandomTransactionGenerator()(1*COIN,1u);
    addTransactionToMockWalletRecord(tx);

    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault(Return(false));
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(tx.GetHash()))).WillByDefault(Return(0));

    BOOST_CHECK_EQUAL(calculator.getBalance(),CAmount(0));
}

BOOST_AUTO_TEST_CASE(willNotIgnoreUnconfirmedTransactionsDebitingFunds)
{
    CTransaction tx = RandomTransactionGenerator()(1*COIN,1u);
    addTransactionToMockWalletRecord(tx);
    CTransaction spendingTx = createRandomSpendingTransaction(tx);
    addTransactionToMockWalletRecord(spendingTx);

    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(tx.GetHash()))).WillByDefault(Return(1));
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(spendingTx.GetHash()))).WillByDefault(Return(0));
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));

    CAmount spentFunds = 0;
    std::set<unsigned> spentOutputIndices = {};
    for(const auto& input: spendingTx.vin)
    {
        spentOutputIndices.insert(input.prevout.n);
        spentFunds+= tx.vout[input.prevout.n].nValue;
    }
    const uint256 txidOfPartiallySpentTx = tx.GetHash();
    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault(
        Invoke(
            [txidOfPartiallySpentTx,spentOutputIndices](const uint256& hash, unsigned int n, const int minimumConfirmations) -> bool
            {
                return hash == txidOfPartiallySpentTx && spentOutputIndices.count(n) > 0;
            }
        )
    );

    BOOST_CHECK_EQUAL(calculator.getBalance(), tx.GetValueOut() + spendingTx.GetValueOut() - spentFunds );
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

BOOST_AUTO_TEST_CASE(unconfirmedCoinbaseTransactionsDoNotContributeToBalance)
{
    CMutableTransaction txTemplate = RandomTransactionGenerator()();
    txTemplate.vin.resize(1);
    txTemplate.vin[0].prevout.SetNull();
    CTransaction tx = txTemplate;
    assert(tx.IsCoinBase());
    addTransactionToMockWalletRecord(tx);
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(_)).WillByDefault(Return(0));
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault(Return(false));
    BOOST_CHECK_EQUAL(calculator.getBalance(),CAmount(0));
}

BOOST_AUTO_TEST_CASE(confirmedButImmatureCoinbaseTransactionsDoNotContributeToBalance)
{
    CMutableTransaction txTemplate = RandomTransactionGenerator()();
    txTemplate.vin.resize(1);
    txTemplate.vin[0].prevout.SetNull();
    CTransaction tx = txTemplate;
    assert(tx.IsCoinBase());
    addTransactionToMockWalletRecord(tx);
    ON_CALL(confsCalculator,GetBlocksToMaturity(_)).WillByDefault(Return(1));
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(_)).WillByDefault(Return(1));
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault(Return(false));
    BOOST_CHECK_EQUAL(calculator.getBalance(),CAmount(0));
}

BOOST_AUTO_TEST_CASE(willIgnoreUnconfirmedCoinstakeTransactions)
{
    CTransaction tx = RandomTransactionGenerator()(1*COIN,1u);
    addTransactionToMockWalletRecord(tx);
    CMutableTransaction coinstakeTxTemplate = createRandomSpendingTransaction(tx,2u);
    coinstakeTxTemplate.vout[0].SetEmpty();
    CTransaction coinstakeTx = coinstakeTxTemplate;
    assert(coinstakeTx.IsCoinStake());
    addTransactionToMockWalletRecord(coinstakeTx);

    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(tx.GetHash()))).WillByDefault(Return(1));
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(coinstakeTx.GetHash()))).WillByDefault(Return(0));
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));

    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault( Return(false) );
    BOOST_CHECK_EQUAL(calculator.getBalance(), tx.GetValueOut() );
}

BOOST_AUTO_TEST_CASE(willIgnoreConfirmedButImmatureCoinstakeTransactions)
{
    CTransaction tx = RandomTransactionGenerator()(1*COIN,1u);
    addTransactionToMockWalletRecord(tx);
    CMutableTransaction coinstakeTxTemplate = createRandomSpendingTransaction(tx,2u);
    coinstakeTxTemplate.vout[0].SetEmpty();
    CTransaction coinstakeTx = coinstakeTxTemplate;
    assert(coinstakeTx.IsCoinStake());
    addTransactionToMockWalletRecord(coinstakeTx);

    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(tx.GetHash()))).WillByDefault(Return(1));
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(coinstakeTx.GetHash()))).WillByDefault(Return(1));
    ON_CALL(confsCalculator,GetBlocksToMaturity(getWalletTx(coinstakeTx.GetHash()))).WillByDefault(Return(1));
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));

    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault( Return(false) );
    BOOST_CHECK_EQUAL(calculator.getBalance(), tx.GetValueOut() );
}

BOOST_AUTO_TEST_CASE(willCountConfirmedAndMatureCoinbaseTransactionsInBalance)
{
    CMutableTransaction txTemplate = RandomTransactionGenerator()();
    txTemplate.vin.resize(1);
    txTemplate.vin[0].prevout.SetNull();
    CTransaction tx = txTemplate;
    assert(tx.IsCoinBase());
    addTransactionToMockWalletRecord(tx);
    ON_CALL(confsCalculator,GetBlocksToMaturity(_)).WillByDefault(Return(0));
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(_)).WillByDefault(Return(1));
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault(Return(false));
    BOOST_CHECK_EQUAL(calculator.getBalance(),tx.GetValueOut());
}

BOOST_AUTO_TEST_CASE(willCountConfirmedAndMatureCoinstakeTransactionsInBalance)
{
    CTransaction tx = RandomTransactionGenerator()(1*COIN,1u);
    addTransactionToMockWalletRecord(tx);
    CMutableTransaction coinstakeTxTemplate = createRandomSpendingTransaction(tx,2u);
    coinstakeTxTemplate.vout[0].SetEmpty();
    CTransaction coinstakeTx = coinstakeTxTemplate;
    assert(coinstakeTx.IsCoinStake());
    addTransactionToMockWalletRecord(coinstakeTx);

    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(tx.GetHash()))).WillByDefault(Return(1));
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(coinstakeTx.GetHash()))).WillByDefault(Return(1));
    ON_CALL(confsCalculator,GetBlocksToMaturity(getWalletTx(coinstakeTx.GetHash()))).WillByDefault(Return(0));
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));

    CAmount debitedAmount =0;
    for(const CTxIn& input: coinstakeTx.vin)
    {
        ON_CALL(spentOutputTracker,IsSpent(input.prevout.hash,input.prevout.n,_))
            .WillByDefault( Return(true) );
        debitedAmount +=tx.vout[input.prevout.n].nValue;
    }

    BOOST_CHECK_EQUAL(calculator.getBalance(), tx.GetValueOut() + coinstakeTx.GetValueOut() - debitedAmount );
}

BOOST_AUTO_TEST_CASE(willEnsureThatATransactionSpendingOnlyUnownedFundsStillNeedsOneConfirmation)
{
    CTransaction tx = RandomTransactionGenerator()(1*COIN);
    for(const CTxOut& output: tx.vout)
    {
            ON_CALL(utxoOwnershipDetector,isMine(output)).WillByDefault(Return(isminetype::ISMINE_NO));
    }
    addTransactionToMockWalletRecord(tx);

    CTransaction spendingTx = createRandomSpendingTransaction(tx,2u);
    for(const CTxOut& output: spendingTx.vout)
    {
            ON_CALL(utxoOwnershipDetector,isMine(output)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
    }
    addTransactionToMockWalletRecord(spendingTx);

    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(tx.GetHash()))).WillByDefault(Return(1));
    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(spendingTx.GetHash()))).WillByDefault(Return(0));

    CAmount debitedAmount =0;
    for(const CTxIn& input: spendingTx.vin)
    {
        ON_CALL(spentOutputTracker,IsSpent(input.prevout.hash,input.prevout.n,_))
            .WillByDefault( Return(true) );
        debitedAmount +=tx.vout[input.prevout.n].nValue;
    }

    BOOST_CHECK_EQUAL(calculator.getBalance(), CAmount(0) );
}

BOOST_AUTO_TEST_CASE(willEnsureThatMaturedButUnconfirmedTransactionsAreRecordedInUnconfirmeBalance)
{
    CMutableTransaction mutableTx = RandomTransactionGenerator()(1*COIN);
    const CTxOut ownedUtxo = mutableTx.vout[0];
    ON_CALL(utxoOwnershipDetector,isMine(ownedUtxo)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
    for(const CTxOut& output: mutableTx.vout)
    {
        if(ownedUtxo != output)
            ON_CALL(utxoOwnershipDetector,isMine(output)).WillByDefault(Return(isminetype::ISMINE_NO));
    }
    std::random_shuffle(mutableTx.vout.begin(),mutableTx.vout.end());
    CTransaction tx(mutableTx);
    addTransactionToMockWalletRecord(tx);

    ON_CALL(confsCalculator,GetNumberOfBlockConfirmations(getWalletTx(tx.GetHash()))).WillByDefault(Return(0));
    ON_CALL(confsCalculator,GetBlocksToMaturity(getWalletTx(tx.GetHash()))).WillByDefault(Return(0));

    BOOST_CHECK_EQUAL(calculator.getUnconfirmedBalance(), ownedUtxo.nValue);
}

BOOST_AUTO_TEST_SUITE_END()