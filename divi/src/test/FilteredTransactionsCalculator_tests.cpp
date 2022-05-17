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
    CWalletTx tx_;
    std::map<uint256, CWalletTx> walletTransactions;

    MockTransactionRecord txRecord;
    MockMerkleTxConfirmationNumberCalculator confsCalculator;
    MockUtxoBalanceCalculator utxoBalanceCalculator;
    FilteredTransactionsCalculator<CAmount> txBalancesCalculator;
    FilteredTransactionsCalculatorTestFixture(
        ): tx_()
        , walletTransactions()
        , txRecord()
        , confsCalculator()
        , utxoBalanceCalculator()
        , txBalancesCalculator(txRecord,confsCalculator,utxoBalanceCalculator)
    {
    }

    void setWalletTransactionRecordToSingleTx()
    {
        walletTransactions.clear();
        tx_ = RandomTransactionGenerator()();
        walletTransactions[tx_.GetHash()] = tx_;
        assert(!tx_.IsCoinBase() && !tx_.IsCoinStake());
    }
    void setWalletTransactionRecordToSingleCoinbaseTx()
    {
        walletTransactions.clear();
        tx_ = RandomTransactionGenerator()();
        tx_.vin.resize(1);
        tx_.vin[0].prevout.SetNull();

        walletTransactions[tx_.GetHash()] = tx_;
        assert(tx_.IsCoinBase());
    }
    void setWalletTransactionRecordToSingleCoinstakeTx()
    {
        walletTransactions.clear();
        tx_ = RandomTransactionGenerator()();
        CTxOut emptyOutput;
        emptyOutput.SetEmpty();
        tx_.vout.insert(tx_.vout.begin(),emptyOutput);

        walletTransactions[tx_.GetHash()] = tx_;
        assert(tx_.IsCoinStake());
    }

    const std::set<TxFlag>& allTxFlags() const
    {
        static const std::set<TxFlag> setOfTxFlags = {
            TxFlag::UNCONFIRMED,
            TxFlag::CONFIRMED,
            TxFlag::IMMATURE,
            TxFlag::MATURE,
            TxFlag::CONFIRMED_AND_MATURE,
            TxFlag::CONFIRMED_AND_IMMATURE};

        return setOfTxFlags;
    }
};

BOOST_FIXTURE_TEST_SUITE(FilteredTransactionCalculatorTests, FilteredTransactionsCalculatorTestFixture)

BOOST_AUTO_TEST_CASE(willApplyCalculationToConfirmedTransactions)
{
    setWalletTransactionRecordToSingleTx();
    ON_CALL(txRecord,GetWalletTransactions()).WillByDefault(ReturnRef(walletTransactions));
    ON_CALL(confsCalculator, GetNumberOfBlockConfirmations(_)).WillByDefault(Return(1));
    ON_CALL(confsCalculator, GetBlocksToMaturity(_)).WillByDefault(Return(GetRandInt(1)));
    EXPECT_CALL(utxoBalanceCalculator,calculate(_,_,_,_)).Times(1);

    UtxoOwnershipFilter filter;
    CAmount totalBalance = 0;
    txBalancesCalculator.applyCalculationToMatchingTransactions(TxFlag::CONFIRMED,filter,totalBalance);
}

BOOST_AUTO_TEST_CASE(willApplyCalculationToUnconfirmedTransactionThatIsntReward)
{
    setWalletTransactionRecordToSingleTx();
    ON_CALL(txRecord,GetWalletTransactions()).WillByDefault(ReturnRef(walletTransactions));
    ON_CALL(confsCalculator, GetNumberOfBlockConfirmations(_)).WillByDefault(Return(0));
    ON_CALL(confsCalculator, GetBlocksToMaturity(_)).WillByDefault(Return(GetRandInt(1)));
    EXPECT_CALL(utxoBalanceCalculator,calculate(_,_,_,_)).Times(1);

    UtxoOwnershipFilter filter;
    CAmount totalBalance = 0;
    txBalancesCalculator.applyCalculationToMatchingTransactions(TxFlag::UNCONFIRMED,filter,totalBalance);
}

BOOST_AUTO_TEST_CASE(willNotApplyCalculationToUnconfirmedCoinbaseTransactions)
{
    setWalletTransactionRecordToSingleCoinbaseTx();
    ON_CALL(txRecord,GetWalletTransactions()).WillByDefault(ReturnRef(walletTransactions));
    ON_CALL(confsCalculator, GetNumberOfBlockConfirmations(_)).WillByDefault(Return(0));
    ON_CALL(confsCalculator, GetBlocksToMaturity(_)).WillByDefault(Return(GetRandInt(1)));
    EXPECT_CALL(utxoBalanceCalculator,calculate(_,_,_,_)).Times(0);

    for(auto txFlag: allTxFlags())
    {
        UtxoOwnershipFilter filter;
        CAmount totalBalance = 0;
        txBalancesCalculator.applyCalculationToMatchingTransactions(txFlag,filter,totalBalance);
    }
}
BOOST_AUTO_TEST_CASE(willNotApplyCalculationToUnconfirmedCoinstakeTransactions)
{
    setWalletTransactionRecordToSingleCoinstakeTx();
    ON_CALL(txRecord,GetWalletTransactions()).WillByDefault(ReturnRef(walletTransactions));
    ON_CALL(confsCalculator, GetNumberOfBlockConfirmations(_)).WillByDefault(Return(0));
    ON_CALL(confsCalculator, GetBlocksToMaturity(_)).WillByDefault(Return(GetRandInt(1)));
    EXPECT_CALL(utxoBalanceCalculator,calculate(_,_,_,_)).Times(0);

    for(auto txFlag: allTxFlags())
    {
        UtxoOwnershipFilter filter;
        CAmount totalBalance = 0;
        txBalancesCalculator.applyCalculationToMatchingTransactions(txFlag,filter,totalBalance);
    }
}

BOOST_AUTO_TEST_CASE(willIgnoreConflictedTransactions)
{
    setWalletTransactionRecordToSingleTx();
    ON_CALL(txRecord,GetWalletTransactions()).WillByDefault(ReturnRef(walletTransactions));
    ON_CALL(confsCalculator, GetNumberOfBlockConfirmations(_)).WillByDefault(Return(-1));
    ON_CALL(confsCalculator, GetBlocksToMaturity(_)).WillByDefault(Return(GetRandInt(1)));
    EXPECT_CALL(utxoBalanceCalculator,calculate(_,_,_,_)).Times(0);

    for(auto txFlag: allTxFlags())
    {
        UtxoOwnershipFilter filter;
        CAmount totalBalance = 0;
        txBalancesCalculator.applyCalculationToMatchingTransactions(txFlag,filter,totalBalance);
    }
}

BOOST_AUTO_TEST_CASE(willNotCalculateAnythingWithoutAnyTransactions)
{
    ON_CALL(txRecord,GetWalletTransactions()).WillByDefault(ReturnRef(walletTransactions));
    ON_CALL(confsCalculator, GetNumberOfBlockConfirmations(_)).WillByDefault(Return(1));
    ON_CALL(confsCalculator, GetBlocksToMaturity(_)).WillByDefault(Return(GetRandInt(0)));
    EXPECT_CALL(utxoBalanceCalculator,calculate(_,_,_,_)).Times(0);

    for(auto txFlag: allTxFlags())
    {
        UtxoOwnershipFilter filter;
        CAmount totalBalance = 0;
        txBalancesCalculator.applyCalculationToMatchingTransactions(txFlag,filter,totalBalance);
    }
}

BOOST_AUTO_TEST_SUITE_END()