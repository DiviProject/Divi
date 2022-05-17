#include <test_only.h>

#include <UtxoBalanceCalculator.h>
#include <MockUtxoOwnershipDetector.h>
#include <MockSpentOutputTracker.h>
#include <WalletTx.h>

#include <random.h>
#include <RandomTransactionGenerator.h>
#include <set>
#include <map>

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

BOOST_AUTO_TEST_CASE(willIgnoreAllUnspentUtxosThatAreNotOwned)
{
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_NO));
    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault(Return(false));
    UtxoOwnershipFilter filter(isminetype::ISMINE_SPENDABLE);
    CWalletTx tx = RandomTransactionGenerator()();
    CAmount totalBalance = 0;
    calculator.calculate(tx,GetRandInt(100),filter,totalBalance);
    BOOST_CHECK_EQUAL(totalBalance, 0*COIN);
}

BOOST_AUTO_TEST_CASE(willTotalAllUnspentUtxosThatAreOwned)
{
    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(Return(isminetype::ISMINE_SPENDABLE));
    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault(Return(false));
    UtxoOwnershipFilter filter(isminetype::ISMINE_SPENDABLE);
    CWalletTx tx = RandomTransactionGenerator()();
    CAmount totalBalance = 0;
    calculator.calculate(tx,GetRandInt(100),filter,totalBalance);
    BOOST_CHECK_EQUAL(totalBalance, tx.GetValueOut());
}

BOOST_AUTO_TEST_CASE(willTotalAllUnspentUtxosThatAreOwnedInTransactionWithMixedOwnershipAndSpentStatus)
{
    CWalletTx tx = RandomTransactionGenerator()(10*COIN, 1u, 20u);
    std::vector<CTxOut> ownedOutputs;
    std::set<unsigned> unspentOutputs;
    unsigned outputIndex = 0u;
    CAmount totalBalanceExpected = 0;
    for(const CTxOut& output: tx.vout)
    {
        const unsigned scenario = GetRandInt(4);
        const bool isOwned = static_cast<bool>(scenario & 1);
        const bool isUnspent = static_cast<bool>(scenario & 2);
        if(isOwned)
        {
            ownedOutputs.push_back(output);
        }
        if(isUnspent)
        {
            unspentOutputs.insert(outputIndex);
        }
        totalBalanceExpected += (isOwned && isUnspent)? output.nValue : 0;
        ++outputIndex;
    }


    ON_CALL(utxoOwnershipDetector,isMine(_)).WillByDefault(
        Invoke(
            [ownedOutputs](const CTxOut& output){
                const bool isOwnedOutput = std::find(ownedOutputs.begin(),ownedOutputs.end(),output) != ownedOutputs.end();
                return isOwnedOutput ? isminetype::ISMINE_SPENDABLE : isminetype::ISMINE_NO;
            }
        )
    );
    ON_CALL(spentOutputTracker,IsSpent(_,_,_)).WillByDefault(
        Invoke(
            [unspentOutputs](const uint256& hash, unsigned int n, const int minimumConfirmations){
                return unspentOutputs.count(n)==0;
            }
        )
    );

    UtxoOwnershipFilter filter(isminetype::ISMINE_SPENDABLE);
    CAmount totalBalance = 0;
    calculator.calculate(tx,GetRandInt(100),filter,totalBalance);
    BOOST_CHECK_EQUAL(totalBalance, totalBalanceExpected);
}

BOOST_AUTO_TEST_SUITE_END()