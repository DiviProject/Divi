#include <test_only.h>
#include <VaultManager.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <RandomCScriptGenerator.h>
#include <memory>
#include <FakeBlockIndexChain.h>
#include <primitives/block.h>
#include <chain.h>

struct VaultManagerTestFixture
{
private:
    std::string testFixtureDummyFilename;
    std::vector<CScript> managedScripts;
public:
    std::unique_ptr<FakeBlockIndexWithHashes> fakeBlockIndexWithHashesResource;
    RandomCScriptGenerator scriptGenerator;
    std::unique_ptr<VaultManager> manager;

    VaultManagerTestFixture(
        ): testFixtureDummyFilename("DummyFilename")
        , managedScripts()
        , fakeBlockIndexWithHashesResource(new FakeBlockIndexWithHashes(1,0,CBlock::CURRENT_VERSION))
        , scriptGenerator()
        , manager( new VaultManager(
            *(fakeBlockIndexWithHashesResource->activeChain),
            *(fakeBlockIndexWithHashesResource->blockIndexByHash),
            testFixtureDummyFilename ))
    {
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
    manager->SyncTransaction(tx,&blockMiningFirstTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(),0u);
}

BOOST_AUTO_TEST_CASE(willNotAddUTXOsFromATxContainingUnknownScripts)
{
    CScript knownScript = scriptGenerator(10);
    CScript unknownScript = scriptGenerator(10);
    manager->addManagedScript(knownScript,1);
    CMutableTransaction tx;
    tx.vout.push_back( CTxOut(100,unknownScript) );

    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->SyncTransaction(tx,&blockMiningFirstTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(),0u);
}

BOOST_AUTO_TEST_CASE(willAddUTXOsOfManagedScripts)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript,1);
    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));

    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->SyncTransaction(tx,&blockMiningFirstTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(),1u);
}

BOOST_AUTO_TEST_CASE(willAddUTXOsFromATxContainingKnownScriptsEvenIfUnknownScriptsArePresent)
{
    CScript knownScript = scriptGenerator(10);
    CScript unknownScript = scriptGenerator(10);
    manager->addManagedScript(knownScript,1);
    CMutableTransaction tx;
    tx.vout.push_back( CTxOut(100,unknownScript) );
    tx.vout.push_back( CTxOut(100,knownScript) );

    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->SyncTransaction(tx,&blockMiningFirstTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(),1u);
}

BOOST_AUTO_TEST_CASE(willRecognizeAsManyUTXOsWithKnownScriptsAsAreAvailableInATransaction)
{
    CScript knownScript = scriptGenerator(10);
    CScript unknownScript = scriptGenerator(10);
    manager->addManagedScript(knownScript,5);
    CMutableTransaction tx;
    tx.vout.push_back( CTxOut(100,knownScript) );
    tx.vout.push_back( CTxOut(100,unknownScript) );
    tx.vout.push_back( CTxOut(100,knownScript) );
    tx.vout.push_back( CTxOut(100,unknownScript) );
    tx.vout.push_back( CTxOut(100,knownScript) );

    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->SyncTransaction(tx,&blockMiningFirstTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(),3u);
}


BOOST_AUTO_TEST_CASE(willAddUTXOsOfManagedScriptsUpToSetLimit)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript, 3);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->SyncTransaction(tx,&blockMiningFirstTx);

    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 2u);

    CMutableTransaction otherTx;
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));

    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->SyncTransaction(otherTx,&blockMiningSecondTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 3u);
}

BOOST_AUTO_TEST_CASE(willDiscountSpentUTXOs)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript, 5);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(300,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->SyncTransaction(tx,&blockMiningFirstTx);

    CMutableTransaction otherTx;
    otherTx.vin.emplace_back( COutPoint(tx.GetHash(), 1u) );
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->SyncTransaction(otherTx,&blockMiningSecondTx);

    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 4u);
}

BOOST_AUTO_TEST_CASE(willNotCountUTXOsFromTransactionsWithoutConfirmations)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript, 3);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    manager->SyncTransaction(tx,nullptr);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 0u);
}

BOOST_AUTO_TEST_CASE(willCheckThatCoinbaseTransactionsAreDeepEnoughToSpend)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript, 4);

    CMutableTransaction tx;
    tx.vin.push_back(CTxIn());
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    assert(CTransaction(tx).IsCoinBase());

    CBlock blockMiningTx = getBlockToMineTransaction(tx);
    manager->SyncTransaction(tx,&blockMiningTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 0u);
    mineAdditionalBlocks(20);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 4u);
}

BOOST_AUTO_TEST_CASE(willCheckThatCoinstakeTransactionsAreDeepEnoughToSpend)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript, 4);

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
    manager->SyncTransaction(tx,&blockMiningTx);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 0u);
    mineAdditionalBlocks(20);
    BOOST_CHECK_EQUAL(manager->getUTXOs().size(), 4u);
}
BOOST_AUTO_TEST_SUITE_END()