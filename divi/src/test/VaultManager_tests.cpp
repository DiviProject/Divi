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
    std::vector<CScript> managedScripts;
public:
    std::unique_ptr<FakeBlockIndexWithHashes> fakeBlockIndexWithHashesResource;
    RandomCScriptGenerator scriptGenerator;
    std::unique_ptr<VaultManager> manager;

    VaultManagerTestFixture(
        ): managedScripts()
        , fakeBlockIndexWithHashesResource(new FakeBlockIndexWithHashes(0,0,4))
        , scriptGenerator()
        , manager( new VaultManager() )
    {
    }

    CBlock getBlockToMineTransaction(const CTransaction& tx)
    {
        fakeBlockIndexWithHashesResource->addBlocks(1,4);
        CBlock firstBlockConfirmation(fakeBlockIndexWithHashesResource->activeChain->Tip()->GetBlockHeader());
        firstBlockConfirmation.vtx.push_back(tx);
        return firstBlockConfirmation;
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

BOOST_AUTO_TEST_SUITE_END()