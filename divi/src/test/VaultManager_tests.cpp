#include <test_only.h>
#include <VaultManager.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <RandomCScriptGenerator.h>
#include <memory>
#include <FakeBlockIndexChain.h>
#include <primitives/block.h>
#include <chain.h>
#include <MockVaultManagerDatabase.h>
#include <WalletTx.h>
#include <streams.h>
#include <clientversion.h>
#include <chainparams.h>
#include <test/FakeMerkleTxConfirmationNumberCalculator.h>
#include <gmock/gmock.h>

using ::testing::_;
using ::testing::Invoke;
struct VaultManagerTestFixture
{
private:
    std::vector<CScript> managedScripts;
public:
    std::unique_ptr<MockVaultManagerDatabase> mockPtr;
    std::unique_ptr<FakeBlockIndexWithHashes> fakeBlockIndexWithHashesResource;
    std::unique_ptr<I_MerkleTxConfirmationNumberCalculator> confirmationsCalculator;
    RandomCScriptGenerator scriptGenerator;
    std::unique_ptr<VaultManager> manager;

    VaultManagerTestFixture(
        ): managedScripts()
        , mockPtr(new MockVaultManagerDatabase)
        , fakeBlockIndexWithHashesResource(new FakeBlockIndexWithHashes(1,0,CBlock::CURRENT_VERSION))
        , confirmationsCalculator(
            new FakeMerkleTxConfirmationNumberCalculator(
                *(fakeBlockIndexWithHashesResource->activeChain),
                *(fakeBlockIndexWithHashesResource->blockIndexByHash)
            ))
        , scriptGenerator()
        , manager( new VaultManager(*confirmationsCalculator ))
    {
    }
    ~VaultManagerTestFixture()
    {
        manager.reset();
        fakeBlockIndexWithHashesResource.reset();
        mockPtr.reset();
    }

    CBlock getBlockToMineTransaction(const CTransaction& tx)
    {
        CBlock firstBlockConfirmation;
        firstBlockConfirmation.vtx.push_back(tx);
        fakeBlockIndexWithHashesResource->addSingleBlock(firstBlockConfirmation);
        return firstBlockConfirmation;
    }
    void mineAdditionalBlocks(unsigned additionalBlocks)
    {
        fakeBlockIndexWithHashesResource->addBlocks(additionalBlocks,CBlock::CURRENT_VERSION);
    }

};

BOOST_FIXTURE_TEST_SUITE(VaultManager_tests,VaultManagerTestFixture)

BOOST_AUTO_TEST_CASE(willNotAddUTXOsFromAnEmptyTx)
{
    CMutableTransaction tx;
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(),0u);
}

BOOST_AUTO_TEST_CASE(willNotAddUTXOsFromATxContainingUnknownScripts)
{
    CScript knownScript = scriptGenerator(10);
    CScript unknownScript = scriptGenerator(10);
    manager->addManagedScript(knownScript);
    CMutableTransaction tx;
    tx.vout.push_back( CTxOut(100,unknownScript) );

    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(),0u);
}

BOOST_AUTO_TEST_CASE(willAddUTXOsOfManagedScripts)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);
    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));

    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(),1u);
}

BOOST_AUTO_TEST_CASE(willAddUTXOsFromATxContainingKnownScriptsEvenIfUnknownScriptsArePresent)
{
    CScript knownScript = scriptGenerator(10);
    CScript unknownScript = scriptGenerator(10);
    manager->addManagedScript(knownScript);
    CMutableTransaction tx;
    tx.vout.push_back( CTxOut(100,unknownScript) );
    tx.vout.push_back( CTxOut(100,knownScript) );

    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(),1u);
}

BOOST_AUTO_TEST_CASE(willRecognizeAsManyUTXOsWithKnownScriptsAsAreAvailableInATransaction)
{
    CScript knownScript = scriptGenerator(10);
    CScript unknownScript = scriptGenerator(10);
    manager->addManagedScript(knownScript);
    CMutableTransaction tx;
    tx.vout.push_back( CTxOut(100,knownScript) );
    tx.vout.push_back( CTxOut(100,unknownScript) );
    tx.vout.push_back( CTxOut(100,knownScript) );
    tx.vout.push_back( CTxOut(100,unknownScript) );
    tx.vout.push_back( CTxOut(100,knownScript) );

    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(),3u);
}


BOOST_AUTO_TEST_CASE(willAddUTXOsOfManagedScriptsIncrementally)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx);

    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 2u);

    CMutableTransaction otherTx;
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));

    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 5u);
}

BOOST_AUTO_TEST_CASE(willDiscountSpentUTXOs)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(300,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx);

    CMutableTransaction otherTx;
    otherTx.vin.emplace_back( COutPoint(tx.GetHash(), 1u) );
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx);

    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 4u);
}

BOOST_AUTO_TEST_CASE(willNotCountUTXOsFromTransactionsWithoutConfirmations)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    manager->addTransaction(tx,nullptr);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 0u);
}

BOOST_AUTO_TEST_CASE(willCheckThatCoinbaseTransactionsAreDeepEnoughToSpend)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction tx;
    tx.vin.push_back(CTxIn());
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    assert(CTransaction(tx).IsCoinBase());

    CBlock blockMiningTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 0u);
    mineAdditionalBlocks(20);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 4u);
}

BOOST_AUTO_TEST_CASE(willCheckThatCoinstakeTransactionsAreDeepEnoughToSpend)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction dummyTransaction;
    dummyTransaction.vout.push_back(CTxOut(100,scriptGenerator(10)));

    CMutableTransaction tx;
    tx.vin.push_back(CTxIn(dummyTransaction.GetHash(),0u));
    CTxOut emptyFirstOutput;
    emptyFirstOutput.SetEmpty();
    tx.vout.push_back(emptyFirstOutput);
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    assert(CTransaction(tx).IsCoinStake());

    CBlock blockMiningTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 0u);
    mineAdditionalBlocks(20);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 4u);
}

BOOST_AUTO_TEST_CASE(willLoadTransactionsFromDatabase)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction dummyTransaction;
    dummyTransaction.vout.push_back(CTxOut(100,scriptGenerator(10)));

    CMutableTransaction tx;
    tx.vin.push_back(CTxIn(dummyTransaction.GetHash(),0u));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));

    CBlock blockMiningTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 4u);

    const CWalletTx expectedTx = manager->getTransaction(tx.GetHash());
    CDataStream txStream(SER_DISK, CLIENT_VERSION);
    txStream << manager->getTransaction(tx.GetHash());

    CDataStream serializedManagedScripts(SER_DISK, CLIENT_VERSION);
    serializedManagedScripts << manager->getManagedScriptLimits();


    ON_CALL(*mockPtr, ReadManagedScripts(_)).WillByDefault(Invoke(
        [&serializedManagedScripts](ManagedScripts& managedScripts)
        {
            serializedManagedScripts >> managedScripts;
            return true;
        }
    ));

    ON_CALL(*mockPtr, ReadTx(_,_)).WillByDefault(Invoke(
        [&txStream](const uint64_t txIndex,CWalletTx& returnTx)
        {
            if(txIndex != 0u)
            {
                return false;
            }
            txStream >> returnTx;
            return true;
        }
    ));
    manager.reset(new VaultManager(*confirmationsCalculator, *mockPtr ));

    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 4u);
    BOOST_CHECK(expectedTx==manager->getTransaction(tx.GetHash()));
}


BOOST_AUTO_TEST_CASE(willLoadManyTransactionsFromDatabase)
{
    std::vector<CMutableTransaction> dummyTransactions;
    std::vector<CDataStream> streamOfTransactions(10u,CDataStream(SER_DISK, CLIENT_VERSION));
    std::vector<CWalletTx> expectedTransactions;
    expectedTransactions.reserve(10u);
    dummyTransactions.reserve(10u);
    for(unsigned txCount =0 ; txCount < 10u; ++txCount)
    {
        CAmount randomSentAmount = 100-txCount;
        CMutableTransaction dummyTransaction;
        dummyTransaction.vout.push_back(CTxOut(100,scriptGenerator(10)));
        CMutableTransaction tx;
        tx.vin.push_back(CTxIn(dummyTransaction.GetHash(),0u));
        tx.vout.push_back(CTxOut(randomSentAmount,scriptGenerator(10)));

        dummyTransactions.emplace_back(tx);
        manager->addTransaction(tx,nullptr);

        const CWalletTx expectedTx = manager->getTransaction(tx.GetHash());
        streamOfTransactions[txCount] << expectedTx;
        expectedTransactions.emplace_back(expectedTx);
    }

    ON_CALL(*mockPtr, ReadTx(_,_)).WillByDefault(Invoke(
        [&streamOfTransactions](const uint64_t txIndex,CWalletTx& returnTx)
        {
            if(txIndex < streamOfTransactions.size())
            {
                streamOfTransactions[txIndex] >> returnTx;
                return true;
            }
            return false;
        }
    ));
    manager.reset(new VaultManager(*confirmationsCalculator, *mockPtr ));

    for(unsigned txCount =0 ; txCount < 10u; ++txCount)
    {
         BOOST_CHECK(expectedTransactions[txCount] == manager->getTransaction(dummyTransactions[txCount].GetHash()));
    }
}

BOOST_AUTO_TEST_CASE(willHaveUTXOCountDiminishIfThirdPartySpendsScript)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(300,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx);

    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 2u);

    CScript otherScript = scriptGenerator(10);
    CMutableTransaction otherTx;
    otherTx.vin.emplace_back( COutPoint(tx.GetHash(), 1u) );
    otherTx.vout.push_back(CTxOut(100,otherScript));
    otherTx.vout.push_back(CTxOut(100,otherScript));
    otherTx.vout.push_back(CTxOut(100,otherScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx);

    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 1u);
}

BOOST_AUTO_TEST_SUITE_END()