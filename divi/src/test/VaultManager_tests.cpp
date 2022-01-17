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
using ::testing::NiceMock;
using ::testing::Invoke;
struct VaultManagerTestFixture
{
public:
    std::unique_ptr<NiceMock<MockVaultManagerDatabase>> mockPtr;
    std::unique_ptr<FakeBlockIndexWithHashes> fakeBlockIndexWithHashesResource;
    std::unique_ptr<I_MerkleTxConfirmationNumberCalculator> confirmationsCalculator;
    RandomCScriptGenerator scriptGenerator;
    std::unique_ptr<VaultManager> manager;

    VaultManagerTestFixture(
        ): mockPtr(new NiceMock<MockVaultManagerDatabase>)
        , fakeBlockIndexWithHashesResource(new FakeBlockIndexWithHashes(1,0,CBlock::CURRENT_VERSION))
        , confirmationsCalculator(
            new FakeMerkleTxConfirmationNumberCalculator(
                *(fakeBlockIndexWithHashesResource->activeChain),
                *(fakeBlockIndexWithHashesResource->blockIndexByHash)
            ))
        , scriptGenerator()
        , manager( new VaultManager(*confirmationsCalculator,*mockPtr ))
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
    void reorgBlocks(unsigned additionalBlocks,unsigned reorgDepth=1)
    {
        fakeBlockIndexWithHashesResource->fork(additionalBlocks,reorgDepth);
    }

};

BOOST_FIXTURE_TEST_SUITE(VaultManager_tests,VaultManagerTestFixture)

BOOST_AUTO_TEST_CASE(willNotAddUTXOsFromAnEmptyTx)
{
    CMutableTransaction tx;
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),0u);
}

BOOST_AUTO_TEST_CASE(willNotAddUTXOsFromATxContainingUnknownScripts)
{
    CScript knownScript = scriptGenerator(10);
    CScript unknownScript = scriptGenerator(10);
    manager->addManagedScript(knownScript);
    CMutableTransaction tx;
    tx.vout.push_back( CTxOut(100,unknownScript) );

    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),0u);
}

BOOST_AUTO_TEST_CASE(willAddUTXOsOfManagedScripts)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);
    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));

    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),1u);
}

BOOST_AUTO_TEST_CASE(willNotAddUTXOsOfManagedScriptsIfTheyHaveZeroValue)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);
    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(0,managedScript));

    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),0u);
}

BOOST_AUTO_TEST_CASE(willNotAddTransactionsWithZeroValueUTXOsForManagedScripts)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);
    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(0,managedScript));

    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, true);
    BOOST_CHECK(manager->getTransaction(tx.GetHash()).vout.empty());
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
    manager->addTransaction(tx,&blockMiningFirstTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),1u);
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
    manager->addTransaction(tx,&blockMiningFirstTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),3u);
}


BOOST_AUTO_TEST_CASE(willAddUTXOsOfManagedScriptsIncrementally)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, true);

    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 2u);

    CMutableTransaction otherTx;
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));

    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 5u);
}

BOOST_AUTO_TEST_CASE(willDiscountSpentUTXOs)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(300,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, true);

    CMutableTransaction otherTx;
    otherTx.vin.emplace_back( COutPoint(tx.GetHash(), 1u) );
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx, true);

    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 4u);
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
    manager->addTransaction(tx,nullptr, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 0u);
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
    manager->addTransaction(tx,&blockMiningTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 0u);
    mineAdditionalBlocks(20);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 4u);
}

BOOST_AUTO_TEST_CASE(willCheckThatCoinstakeTransactionsAreDeepEnoughToSpend)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction fundingTransaction;
    fundingTransaction.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFundingTx = getBlockToMineTransaction(fundingTransaction);
    manager->addTransaction(fundingTransaction,&blockMiningFundingTx, true);

    CMutableTransaction tx;
    tx.vin.push_back(CTxIn(fundingTransaction.GetHash(),0u));
    CTxOut emptyFirstOutput;
    emptyFirstOutput.SetEmpty();
    tx.vout.push_back(emptyFirstOutput);
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    assert(CTransaction(tx).IsCoinStake());

    CBlock blockMiningTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningTx, false);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 0u);
    mineAdditionalBlocks(20);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 4u);
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
    manager->addTransaction(tx,&blockMiningTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 4u);

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

    ON_CALL(*mockPtr, ReadTx(_)).WillByDefault(Invoke(
        [&txStream](CWalletTx& returnTx)
        {
            if(txStream.size() == 0u)
            {
                return false;
            }
            txStream >> returnTx;
            txStream.clear();
            return true;
        }
    ));
    manager.reset(new VaultManager(*confirmationsCalculator, *mockPtr ));

    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 4u);
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
        manager->addTransaction(tx,nullptr, true);

        const CWalletTx expectedTx = manager->getTransaction(tx.GetHash());
        streamOfTransactions[txCount] << expectedTx;
        expectedTransactions.emplace_back(expectedTx);
    }

    ON_CALL(*mockPtr, ReadTx(_)).WillByDefault(Invoke(
        [&streamOfTransactions](CWalletTx& returnTx)
        {
            static uint64_t txIndex = 0;
            if(txIndex < streamOfTransactions.size())
            {
                streamOfTransactions[txIndex++] >> returnTx;
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
    manager->addTransaction(tx,&blockMiningFirstTx, true);

    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 2u);

    CScript otherScript = scriptGenerator(10);
    CMutableTransaction otherTx;
    otherTx.vin.emplace_back( COutPoint(tx.GetHash(), 1u) );
    otherTx.vout.push_back(CTxOut(100,otherScript));
    otherTx.vout.push_back(CTxOut(100,otherScript));
    otherTx.vout.push_back(CTxOut(100,otherScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx, true);

    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 1u);
}

BOOST_AUTO_TEST_CASE(willNotRecoverDepositUTXOsAfterReorg)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(300,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, true);

    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 2u);
    mineAdditionalBlocks(1);

    CScript otherScript = scriptGenerator(10);
    CMutableTransaction otherTx;
    otherTx.vin.emplace_back( COutPoint(tx.GetHash(), 0u) );
    otherTx.vin.emplace_back( COutPoint(tx.GetHash(), 1u) );
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx, true);

    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 3u);

    reorgBlocks(1);

    const CWalletTx& firstTx = manager->getTransaction(tx.GetHash());
    const CWalletTx& secondTx = manager->getTransaction(otherTx.GetHash());
    BOOST_CHECK_EQUAL(confirmationsCalculator->GetNumberOfBlockConfirmations(firstTx),3);
    BOOST_CHECK_EQUAL(confirmationsCalculator->GetNumberOfBlockConfirmations(secondTx),0);

    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 0u);
}

BOOST_AUTO_TEST_CASE(willRecoverPreviouslyStakedUTXOsAfterReorg)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(300,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, true);

    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 2u);
    mineAdditionalBlocks(1);

    CScript otherScript = scriptGenerator(10);
    CMutableTransaction otherTx;
    otherTx.vin.emplace_back( COutPoint(tx.GetHash(), 0u) );
    otherTx.vin.emplace_back( COutPoint(tx.GetHash(), 1u) );
    otherTx.vout.emplace_back();
    otherTx.vout.back().SetEmpty();
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));
    otherTx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx, true);

    assert(CTransaction(otherTx).IsCoinStake());
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 0u);
    mineAdditionalBlocks(20);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 3u);

    reorgBlocks(1,21);

    const CWalletTx& firstTx = manager->getTransaction(tx.GetHash());
    const CWalletTx& secondTx = manager->getTransaction(otherTx.GetHash());
    BOOST_CHECK_EQUAL(confirmationsCalculator->GetNumberOfBlockConfirmations(firstTx),3);
    BOOST_CHECK_EQUAL(confirmationsCalculator->GetNumberOfBlockConfirmations(secondTx),-1);

    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 2u);
    CBlock reminingBlock = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&reminingBlock, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 0u);
    mineAdditionalBlocks(20);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 3u);
}

BOOST_AUTO_TEST_CASE(willNotUseUTXOsOfABlockThatsBeenDisconnected)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(300,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);

    manager->addTransaction(tx,&blockMiningFirstTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 2u);

    manager->addTransaction(tx,nullptr,false);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(), 0u);
}

BOOST_AUTO_TEST_CASE(willRecordInTheWalletTxWetherTransactionWasADeposit)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(300,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, true);

    CScript otherScript = scriptGenerator(10);
    CMutableTransaction otherTx;
    otherTx.vin.emplace_back( COutPoint(tx.GetHash(), 1u) );
    otherTx.vout.push_back(CTxOut(100,otherScript));
    otherTx.vout.push_back(CTxOut(100,otherScript));
    otherTx.vout.push_back(CTxOut(100,otherScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx, false);

    const CWalletTx& firstTx = manager->getTransaction(tx.GetHash());
    const CWalletTx& secondTx = manager->getTransaction(otherTx.GetHash());
    BOOST_CHECK_EQUAL(firstTx.mapValue.count("isVaultDeposit"),1u);
    BOOST_CHECK_EQUAL(secondTx.mapValue.count("isVaultDeposit"),0u);
}

BOOST_AUTO_TEST_CASE(willStopRecognizingUTXOsAsManagedWhenNotAllInputsAreKnown)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CScript dummyScript = scriptGenerator(10);
    CMutableTransaction dummyTransaction;
    dummyTransaction.vout.push_back(CTxOut(100,dummyScript));
    CBlock blockMiningDummyTx = getBlockToMineTransaction(dummyTransaction);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),1u);

    CMutableTransaction otherTx;
    otherTx.vin.emplace_back( COutPoint(tx.GetHash(), 0u) );
    otherTx.vin.emplace_back( COutPoint(dummyTransaction.GetHash(), 0u) );
    otherTx.vout.push_back(CTxOut(50,managedScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx, false);

    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),0u);
}

BOOST_AUTO_TEST_CASE(willStopRecognizingUTXOsAsManagedWhenNotAllInputsAreManaged)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CScript dummyScript = scriptGenerator(10);
    CMutableTransaction dummyTransaction;
    dummyTransaction.vout.push_back(CTxOut(100,dummyScript));
    CBlock blockMiningDummyTx = getBlockToMineTransaction(dummyTransaction);
    manager->addTransaction(dummyTransaction,&blockMiningDummyTx, false);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),1u);

    CMutableTransaction otherTx;
    otherTx.vin.emplace_back( COutPoint(tx.GetHash(), 0u) );
    otherTx.vin.emplace_back( COutPoint(dummyTransaction.GetHash(), 0u) );
    otherTx.vout.push_back(CTxOut(50,managedScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx, false);

    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),0u);
}

BOOST_AUTO_TEST_CASE(willIgnoreManagedUTXOsIfNotSpentByCoinstakeNorDeposited)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CScript otherManagedScript = scriptGenerator(10);
    manager->addManagedScript(otherManagedScript);

    CMutableTransaction otherTx;
    otherTx.vout.push_back(CTxOut(100,otherManagedScript));
    otherTx.vout.push_back(CTxOut(100,otherManagedScript));
    CBlock blockMiningDummyTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningDummyTx, true);

    CMutableTransaction tx;
    tx.vout.push_back(CTxOut(100,managedScript));
    tx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),4u);

    CMutableTransaction spendingTx;
    spendingTx.vin.emplace_back( COutPoint(tx.GetHash(), 0u) );
    spendingTx.vin.emplace_back( COutPoint(otherTx.GetHash(), 0u) );
    spendingTx.vout.push_back(CTxOut(50,managedScript));
    spendingTx.vout.push_back(CTxOut(50,managedScript));
    spendingTx.vout.push_back(CTxOut(50,managedScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(spendingTx);
    manager->addTransaction(spendingTx,&blockMiningSecondTx, false);

    assert(!CTransaction(spendingTx).IsCoinStake());
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),2u);

    CMutableTransaction secondSpendingTx;
    secondSpendingTx.vin.emplace_back( COutPoint(tx.GetHash(), 1u) );
    secondSpendingTx.vin.emplace_back( COutPoint(otherTx.GetHash(), 1u) );
    secondSpendingTx.vout.emplace_back();
    secondSpendingTx.vout.back().SetEmpty();
    secondSpendingTx.vout.push_back(CTxOut(50,managedScript));
    secondSpendingTx.vout.push_back(CTxOut(50,managedScript));
    secondSpendingTx.vout.push_back(CTxOut(50,managedScript));
    CBlock blockMiningThirdTx = getBlockToMineTransaction(secondSpendingTx);

    BOOST_CHECK(secondSpendingTx.vin.size() > 0);
    BOOST_CHECK(!secondSpendingTx.vin[0].prevout.IsNull());
    BOOST_CHECK(secondSpendingTx.vout.size() >= 2);
    BOOST_CHECK(secondSpendingTx.vout[0].IsEmpty());

    manager->addTransaction(secondSpendingTx,&blockMiningThirdTx, false);
    mineAdditionalBlocks(20);

    assert(CTransaction(secondSpendingTx).IsCoinStake());
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),3u);
}

BOOST_AUTO_TEST_CASE(willAcceptDepositBasedUTXOsEvenIfInputsArentKnown)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CScript dummyScript = scriptGenerator(10);
    CMutableTransaction dummyTransaction;
    dummyTransaction.vout.push_back(CTxOut(100,dummyScript));
    CBlock blockMiningDummyTx = getBlockToMineTransaction(dummyTransaction);

    CMutableTransaction tx;
    tx.vin.emplace_back( COutPoint(dummyTransaction.GetHash(), 0u) );
    tx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),1u);
}

BOOST_AUTO_TEST_CASE(willAllowUpdatingUTXOsToDepositStatus)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CScript dummyScript = scriptGenerator(10);
    CMutableTransaction dummyTransaction;
    dummyTransaction.vout.push_back(CTxOut(100,dummyScript));
    CBlock blockMiningDummyTx = getBlockToMineTransaction(dummyTransaction);

    CMutableTransaction tx;
    tx.vin.emplace_back( COutPoint(dummyTransaction.GetHash(), 0u) );
    tx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, false);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),0u);
    manager->addTransaction(tx,&blockMiningFirstTx, true);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),1u);
}

BOOST_AUTO_TEST_CASE(willUpdatingDepositStatusWillPersist)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    std::vector<CDataStream> mockDBCache;
    ON_CALL(*mockPtr,WriteTx(_)).WillByDefault(
        Invoke(
            [&mockDBCache](const CWalletTx& walletTx)-> bool
            {
                mockDBCache.emplace_back(SER_DISK, CLIENT_VERSION);
                mockDBCache.back() << walletTx;
                return true;
            }
        )
    );
    ON_CALL(*mockPtr,ReadTx(_)).WillByDefault(
        Invoke(
            [&mockDBCache](CWalletTx& walletTx)-> bool
            {
                if(mockDBCache.size()==0) return false;
                mockDBCache.back() >> walletTx;
                mockDBCache.pop_back();
                return true;
            }
        )
    );

    CScript dummyScript = scriptGenerator(10);
    CMutableTransaction dummyTransaction;
    dummyTransaction.vout.push_back(CTxOut(100,dummyScript));
    CBlock blockMiningDummyTx = getBlockToMineTransaction(dummyTransaction);

    CMutableTransaction tx;
    tx.vin.emplace_back( COutPoint(dummyTransaction.GetHash(), 0u) );
    tx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, false);
    BOOST_CHECK_EQUAL(manager->getTransaction(tx.GetHash()).mapValue.count("isVaultDeposit"),0u);
    manager->addTransaction(tx,&blockMiningFirstTx, true);
    BOOST_CHECK_EQUAL(manager->getTransaction(tx.GetHash()).mapValue.count("isVaultDeposit"),1u);

    manager.reset( new VaultManager(*confirmationsCalculator,*mockPtr ));
    BOOST_CHECK_EQUAL(manager->getTransaction(tx.GetHash()).mapValue.count("isVaultDeposit"),1u);
}

BOOST_AUTO_TEST_CASE(willRecordTransactionsSpendingDeposits)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction fundingTransaction;
    fundingTransaction.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFundingTx = getBlockToMineTransaction(fundingTransaction);
    manager->addTransaction(fundingTransaction,&blockMiningFundingTx, true);

    CMutableTransaction tx;
    tx.vin.emplace_back( COutPoint(fundingTransaction.GetHash(), 0u) );
    tx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, false);

    const auto& walletTx = manager->getTransaction(tx.GetHash());
    BOOST_CHECK_EQUAL(walletTx.vin.size(),1u);
    BOOST_CHECK_EQUAL(walletTx.vout.size(),1u);
}

BOOST_AUTO_TEST_CASE(willNotRecordADepositTransactionThatIsntExplicitlyAdded)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CScript dummyScript = scriptGenerator(10);
    CMutableTransaction fundingTransaction;
    fundingTransaction.vout.push_back(CTxOut(100,dummyScript));
    CBlock blockMiningDummyTx = getBlockToMineTransaction(fundingTransaction);

    CMutableTransaction tx;
    tx.vin.emplace_back( COutPoint(fundingTransaction.GetHash(), 0u) );
    tx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFirstTx = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&blockMiningFirstTx, false);

    const auto& walletTx = manager->getTransaction(tx.GetHash());
    BOOST_CHECK_EQUAL(walletTx.vin.size(),0u);
    BOOST_CHECK_EQUAL(walletTx.vout.size(),0u);
}

BOOST_AUTO_TEST_CASE(willNotIgnoreCoinstakeTransactionWithSingleUnamangedInput)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction dummyTransaction;
    dummyTransaction.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningDummyTx = getBlockToMineTransaction(dummyTransaction);

    CMutableTransaction otherTx;
    otherTx.vin.emplace_back( COutPoint(dummyTransaction.GetHash(), 0u) );
    otherTx.vout.emplace_back();
    otherTx.vout.back().SetEmpty();
    otherTx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx, false);
    mineAdditionalBlocks(20);
    assert(CTransaction(otherTx).IsCoinStake());

    const auto& otherWalletTx = manager->getTransaction(otherTx.GetHash());
    BOOST_CHECK_EQUAL(otherWalletTx.vin.size(),1u);
    BOOST_CHECK_EQUAL(otherWalletTx.vout.size(),2u);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),1u);
}
BOOST_AUTO_TEST_CASE(willNotIgnoreCoinstakeTransactionWithUnmanagedInputWithUnknownScript)
{
    CScript unmanagedScript = scriptGenerator(10);
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction dummyTransaction;
    dummyTransaction.vout.push_back(CTxOut(100,unmanagedScript));

    CMutableTransaction otherTx;
    otherTx.vin.emplace_back( COutPoint(dummyTransaction.GetHash(), 0u) );
    otherTx.vout.emplace_back();
    otherTx.vout.back().SetEmpty();
    otherTx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx, false);
    mineAdditionalBlocks(20);
    assert(CTransaction(otherTx).IsCoinStake());

    const auto& otherWalletTx = manager->getTransaction(otherTx.GetHash());
    BOOST_CHECK_EQUAL(otherWalletTx.vin.size(),1u);
    BOOST_CHECK_EQUAL(otherWalletTx.vout.size(),2u);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),1u);
}

BOOST_AUTO_TEST_CASE(willNotIgnoreOutputsInCoinstakeWithAtLeastOneUnamangedInput)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction dummyTransaction;
    dummyTransaction.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningDummyTx = getBlockToMineTransaction(dummyTransaction);

    const auto& dummyTx = manager->getTransaction(dummyTransaction.GetHash());
    BOOST_CHECK_EQUAL(dummyTx.vin.size(),0u);
    BOOST_CHECK_EQUAL(dummyTx.vout.size(),0u);

    CMutableTransaction fundingTransaction;
    fundingTransaction.vout.push_back(CTxOut(100,managedScript));
    fundingTransaction.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFundingTx = getBlockToMineTransaction(fundingTransaction);
    manager->addTransaction(fundingTransaction,&blockMiningFundingTx, true);

    CMutableTransaction otherTx;
    otherTx.vin.emplace_back( COutPoint(fundingTransaction.GetHash(), 0u) );
    otherTx.vin.emplace_back( COutPoint(dummyTransaction.GetHash(), 0u) );
    otherTx.vin.emplace_back( COutPoint(fundingTransaction.GetHash(), 1u) );
    otherTx.vout.emplace_back();
    otherTx.vout.back().SetEmpty();
    otherTx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx, false);
    mineAdditionalBlocks(20);
    assert(CTransaction(otherTx).IsCoinStake());
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),1u);
}

BOOST_AUTO_TEST_CASE(willRecordCoinstakeTransactionWithOnlyMangedInputs)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CMutableTransaction fundingTransaction;
    fundingTransaction.vout.push_back(CTxOut(100,managedScript));
    fundingTransaction.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningFundingTx = getBlockToMineTransaction(fundingTransaction);
    manager->addTransaction(fundingTransaction,&blockMiningFundingTx, true);

    CMutableTransaction otherTx;
    otherTx.vin.emplace_back( COutPoint(fundingTransaction.GetHash(), 0u) );
    otherTx.vin.emplace_back( COutPoint(fundingTransaction.GetHash(), 1u) );
    otherTx.vout.emplace_back();
    otherTx.vout.back().SetEmpty();
    otherTx.vout.push_back(CTxOut(100,managedScript));
    CBlock blockMiningSecondTx = getBlockToMineTransaction(otherTx);
    manager->addTransaction(otherTx,&blockMiningSecondTx, false);
    assert(CTransaction(otherTx).IsCoinStake());

    const auto& otherWalletTx = manager->getTransaction(otherTx.GetHash());
    BOOST_CHECK_EQUAL(otherWalletTx.vin.size(),2u);
    BOOST_CHECK_EQUAL(otherWalletTx.vout.size(),2u);
}

BOOST_AUTO_TEST_CASE(willNotAddNonDepositUnconfirmedTransactions)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CScript dummyScript = scriptGenerator(10);
    CMutableTransaction dummyTransaction;
    dummyTransaction.vout.push_back(CTxOut(100,dummyScript));
    CBlock blockMiningDummyTx = getBlockToMineTransaction(dummyTransaction);

    CMutableTransaction tx;
    tx.vin.emplace_back( COutPoint(dummyTransaction.GetHash(), 0u) );
    tx.vout.push_back(CTxOut(100,managedScript));
    manager->addTransaction(tx,nullptr, false);

    const auto& walletTx = manager->getTransaction(tx.GetHash());
    BOOST_CHECK_EQUAL(walletTx.vin.size(),0u);
    BOOST_CHECK_EQUAL(walletTx.vout.size(),0u);
}

BOOST_AUTO_TEST_CASE(willNotAddDepositUnconfirmedTransactions)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CScript dummyScript = scriptGenerator(10);
    CMutableTransaction dummyTransaction;
    dummyTransaction.vout.push_back(CTxOut(100,dummyScript));
    CBlock blockMiningDummyTx = getBlockToMineTransaction(dummyTransaction);

    CMutableTransaction tx;
    tx.vin.emplace_back( COutPoint(dummyTransaction.GetHash(), 0u) );
    tx.vout.push_back(CTxOut(100,managedScript));
    manager->addTransaction(tx,nullptr, true);

    const auto& walletTx = manager->getTransaction(tx.GetHash());
    BOOST_CHECK_EQUAL(walletTx.vin.size(),0u);
    BOOST_CHECK_EQUAL(walletTx.vout.size(),0u);
}

BOOST_AUTO_TEST_CASE(willStopRecognizingUTXOsForWhichTheScriptHasBeenRevoked)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CScript dummyScript = scriptGenerator(10);
    CMutableTransaction dummyTransaction;
    dummyTransaction.vout.push_back(CTxOut(100,dummyScript));
    CBlock blockMiningDummyTx = getBlockToMineTransaction(dummyTransaction);

    CMutableTransaction tx;
    tx.vin.emplace_back( COutPoint(dummyTransaction.GetHash(), 0u) );
    tx.vout.push_back(CTxOut(100,managedScript));
    CBlock block = getBlockToMineTransaction(tx);
    manager->addTransaction(tx,&block, true);

    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),1u);
    manager->removeManagedScript(managedScript);
    BOOST_CHECK_EQUAL(manager->getManagedUTXOs().size(),0u);
}

BOOST_AUTO_TEST_CASE(willWriteManagedScriptToBackendOnAddition)
{
    CScript managedScript = scriptGenerator(10);
    EXPECT_CALL(*mockPtr,WriteManagedScript(managedScript)).Times(1);
    manager->addManagedScript(managedScript);
}

BOOST_AUTO_TEST_CASE(willWriteTxToBackendOnAddition)
{
    CScript managedScript = scriptGenerator(10);
    manager->addManagedScript(managedScript);

    CScript dummyScript = scriptGenerator(10);
    CMutableTransaction dummyTransaction;
    dummyTransaction.vout.push_back(CTxOut(100,dummyScript));
    CBlock blockMiningDummyTx = getBlockToMineTransaction(dummyTransaction);

    CMutableTransaction tx;
    tx.vin.emplace_back( COutPoint(dummyTransaction.GetHash(), 0u) );
    tx.vout.push_back(CTxOut(100,managedScript));
    CBlock block = getBlockToMineTransaction(tx);

    CWalletTx walletTx(tx);
    EXPECT_CALL(*mockPtr,WriteTx(walletTx)).Times(1);
    manager->addTransaction(tx,&block, true);
}

BOOST_AUTO_TEST_CASE(willEraseManagedScriptToBackendOnRemoval)
{
    CScript managedScript = scriptGenerator(10);
    EXPECT_CALL(*mockPtr,WriteManagedScript(managedScript)).Times(1);
    manager->addManagedScript(managedScript);
    EXPECT_CALL(*mockPtr,EraseManagedScript(managedScript)).Times(1);
    manager->removeManagedScript(managedScript);
}

BOOST_AUTO_TEST_CASE(willNotWriteManagedScriptToBackendOnAdditionMoreThanOnce)
{
    CScript managedScript = scriptGenerator(10);
    EXPECT_CALL(*mockPtr,WriteManagedScript(managedScript)).Times(1);
    manager->addManagedScript(managedScript);
    manager->addManagedScript(managedScript);
}

BOOST_AUTO_TEST_CASE(willNotEraseManagedScriptToBackendOnRemovalMoreThanOnce)
{
    CScript managedScript = scriptGenerator(10);
    EXPECT_CALL(*mockPtr,WriteManagedScript(managedScript)).Times(1);
    manager->addManagedScript(managedScript);
    EXPECT_CALL(*mockPtr,EraseManagedScript(managedScript)).Times(1);
    manager->removeManagedScript(managedScript);
    manager->removeManagedScript(managedScript);
}

BOOST_AUTO_TEST_CASE(willInvokeDatabaseSyncWhenManagerSyncIsRequested)
{
    EXPECT_CALL(*mockPtr,Sync(_)).Times(1);
    manager->Sync();
}

BOOST_AUTO_TEST_SUITE_END()