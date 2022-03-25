#include <test/test_only.h>
#include <MinimumFeeCoinSelectionAlgorithm.h>
#include <keystore.h>
#include <key.h>
#include <script/standard.h>
#include <RandomCScriptGenerator.h>
#include <vector>
#include <set>
#include <WalletTx.h>
#include <blockmap.h>
#include <chain.h>
#include <chainparams.h>
#include <test/MockSignatureSizeEstimator.h>
#include <FeeRate.h>
#include <test/RandomUtxoGenerator.h>
#include <iostream>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;
class MinimumFeeCoinSelectionAlgorithmTestFixture
{
private:
    CScript addSingleKey(bool compressedKey = true)
    {
        CKey newKey;
        newKey.MakeNewKey(compressedKey);
        keystore_.AddKey(newKey);
        keyIds.push_back(newKey.GetPubKey().GetID());
        return GetScriptForDestination(keyIds.back());
    }

    CBasicKeyStore keystore_;
    BlockMap fakeBlockIndex;
    CChain fakeActiveChain;
    std::vector<CWalletTx> walletTransactions_;

public:
    std::vector<CKeyID> keyIds;
    NiceMock<MockSignatureSizeEstimator> mockSignatureSizeEstimator;
    CFeeRate feeRate;
    MinimumFeeCoinSelectionAlgorithm algorithm;
    RandomCScriptGenerator scriptGenerator;
    CScript smallScriptSigReqScript;
    CScript largeScriptSigReqScript;

    MinimumFeeCoinSelectionAlgorithmTestFixture(
        ): keystore_()
        , fakeBlockIndex()
        , fakeActiveChain()
        , walletTransactions_()
        , keyIds()
        , mockSignatureSizeEstimator()
        , feeRate(10000)
        , algorithm(keystore_,mockSignatureSizeEstimator,feeRate)
        , scriptGenerator()
        , smallScriptSigReqScript(scriptGenerator(25))
        , largeScriptSigReqScript(scriptGenerator(25))
    {
    }

    void addSingleUtxo(const CAmount utxoAmount, CScript customScript)
    {
        CMutableTransaction mutableTx;
        mutableTx.vin.emplace_back();
        mutableTx.vin[0].scriptSig = scriptGenerator(25);
        mutableTx.vout.emplace_back(utxoAmount,customScript);
        walletTransactions_.emplace_back(CTransaction(mutableTx));
    }
    void addSingleUtxo(const CAmount utxoAmount, bool smallScriptSigSize)
    {
        addSingleUtxo(utxoAmount,smallScriptSigSize?smallScriptSigReqScript:largeScriptSigReqScript);
    }

    void createOutputs(const CAmount utxoAmount, const unsigned utxoCount)
    {
        const unsigned finalUTXOCount = walletTransactions_.size() + utxoCount;
        while(walletTransactions_.size() < finalUTXOCount)
        {
            if(walletTransactions_.size() >= finalUTXOCount) break;
            addSingleUtxo(utxoAmount,true);
            if(walletTransactions_.size() >= finalUTXOCount) break;
            addSingleUtxo(utxoAmount,false);
        }
    }

    std::vector<COutput> getSpendableOutputs()
    {
        std::vector<COutput> outputs;
        outputs.reserve(walletTransactions_.size());
        for(const CWalletTx& walletTx: walletTransactions_)
        {
            outputs.emplace_back(&walletTx,0u,50u,true);
        }
        return outputs;
    }
};

BOOST_FIXTURE_TEST_SUITE(MinimumFeeCoinSelectionAlgorithm_tests, MinimumFeeCoinSelectionAlgorithmTestFixture)

BOOST_AUTO_TEST_CASE(willPreferentiallyUseUTXOsWithSmallerScriptSigs)
{
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,smallScriptSigReqScript)).WillByDefault(Return(100u));
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,largeScriptSigReqScript)).WillByDefault(Return(101u));

    createOutputs(CAmount(100*COIN),20u);
    std::vector<COutput> utxos = getSpendableOutputs();
    CAmount toSpend = 900*COIN;
    CMutableTransaction novelTx;
    novelTx.vout.emplace_back(toSpend,RandomCScriptGenerator()(25u));
    CAmount feesPaidEstimate = 0;
    std::set<COutput> selectedUTXOs = algorithm.SelectCoins(novelTx,utxos,feesPaidEstimate);
    for(const COutput& outputSelected: selectedUTXOs)
    {
        CScript inputScript = outputSelected.scriptPubKey();
        BOOST_CHECK_MESSAGE(
            outputSelected.scriptPubKey()==smallScriptSigReqScript,
            "Unexpected output being used. Paying more in fees than necessary");
    }
}
BOOST_AUTO_TEST_CASE(willUseUTXOsWithPaysLeastFeesAndCoversExpenses)
{
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,smallScriptSigReqScript)).WillByDefault(Return(100u));
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,largeScriptSigReqScript)).WillByDefault(Return(101u));

    addSingleUtxo(1*COIN,false);
    addSingleUtxo(1*COIN,true);

    std::vector<COutput> utxos = getSpendableOutputs();
    CAmount toSpend = 1*CENT;
    CMutableTransaction novelTx;
    novelTx.vout.emplace_back(toSpend,RandomCScriptGenerator()(25u));
    CAmount feesPaidEstimate = 0;
    std::set<COutput> selectedUTXOs = algorithm.SelectCoins(novelTx,utxos,feesPaidEstimate);
    for(const COutput& outputSelected: selectedUTXOs)
    {
        CScript inputScript = outputSelected.scriptPubKey();
        BOOST_CHECK_MESSAGE(
            outputSelected.scriptPubKey()==smallScriptSigReqScript,
            "Unexpected output being used. Paying more in fees than necessary");
    }
}
BOOST_AUTO_TEST_CASE(willUseUTXOsWithPaysLeastFeesAndCoversExpensesEvenIfLargeInValue)
{
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,smallScriptSigReqScript)).WillByDefault(Return(100u));
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,largeScriptSigReqScript)).WillByDefault(Return(101u));

    addSingleUtxo(1*COIN,false);
    addSingleUtxo(10*COIN,true);

    std::vector<COutput> utxos = getSpendableOutputs();
    CAmount toSpend = 1*CENT;
    CMutableTransaction novelTx;
    novelTx.vout.emplace_back(toSpend,RandomCScriptGenerator()(25u));
    CAmount feesPaidEstimate = 0;
    std::set<COutput> selectedUTXOs = algorithm.SelectCoins(novelTx,utxos,feesPaidEstimate);
    for(const COutput& outputSelected: selectedUTXOs)
    {
        CScript inputScript = outputSelected.scriptPubKey();
        BOOST_CHECK_MESSAGE(
            outputSelected.scriptPubKey()==smallScriptSigReqScript,
            "Unexpected output being used. Paying more in fees than necessary");
    }
}

BOOST_AUTO_TEST_CASE(willFailToSelectAnyCoinsIfAmountToSpendExceedsAvailableFunds)
{
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,smallScriptSigReqScript)).WillByDefault(Return(100u));
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,largeScriptSigReqScript)).WillByDefault(Return(101u));

    addSingleUtxo(2*COIN,false);
    addSingleUtxo(1*COIN,true);

    std::vector<COutput> utxos = getSpendableOutputs();
    CAmount toSpend = 10*COIN;
    CMutableTransaction novelTx;
    novelTx.vout.emplace_back(toSpend,RandomCScriptGenerator()(25u));
    CAmount feesPaidEstimate = 0;
    std::set<COutput> selectedUTXOs = algorithm.SelectCoins(novelTx,utxos,feesPaidEstimate);
    BOOST_CHECK_MESSAGE(
            selectedUTXOs.empty(),
            "Non-empty utxo set suggests target spending is reached");
}

BOOST_AUTO_TEST_CASE(willFailToSelectAnyCoinsIfTotalTransactionSizeWouldExceedMaximum)
{
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,smallScriptSigReqScript)).WillByDefault(Return(100000u));
    addSingleUtxo(5*COIN,true);

    std::vector<COutput> utxos = getSpendableOutputs();
    CAmount toSpend = 3*COIN;
    CMutableTransaction novelTx;
    novelTx.vout.emplace_back(toSpend,RandomCScriptGenerator()(25u));
    CAmount feesPaidEstimate = 0;
    std::set<COutput> selectedUTXOs = algorithm.SelectCoins(novelTx,utxos,feesPaidEstimate);
    BOOST_CHECK_MESSAGE(
            selectedUTXOs.empty(),
            "Exceeded maximum transaction size while selecting inputs.");
}

BOOST_AUTO_TEST_CASE(willPreferSelectingSmallerScripSigRequirementUTXOsWhenAmountMinusRelayFeesAreTheSame)
{
    std::vector<CScript> scripts = { scriptGenerator(25),scriptGenerator(25),scriptGenerator(25)};
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,scripts[0])).WillByDefault(Return(100u));
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,scripts[1])).WillByDefault(Return(150u));
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,scripts[2])).WillByDefault(Return(100u));
    addSingleUtxo(4*COIN,scripts[0]);
    addSingleUtxo(2*COIN+1500,scripts[1]);
    addSingleUtxo(2*COIN+1000,scripts[2]);

    std::vector<COutput> utxos = getSpendableOutputs();
    CAmount toSpend = 5*COIN;
    CMutableTransaction novelTx;
    novelTx.vout.emplace_back(toSpend,RandomCScriptGenerator()(25u));
    CAmount feesPaidEstimate = 0;
    std::set<COutput> selectedUTXOs = algorithm.SelectCoins(novelTx,utxos,feesPaidEstimate);
    for(const COutput& outputSelected: selectedUTXOs)
    {
        BOOST_CHECK_MESSAGE(
            outputSelected.scriptPubKey()!=scripts[1],
            "Unexpected output being used. Paying more in fees than necessary");
    }
}

BOOST_AUTO_TEST_CASE(willIncreaseSpendingToCoverInitialFeeEstimateProvided)
{
    std::vector<CScript> scripts = { scriptGenerator(25),scriptGenerator(25),scriptGenerator(25)};
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,scripts[0])).WillByDefault(Return(100u));
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,scripts[1])).WillByDefault(Return(150u));
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,scripts[2])).WillByDefault(Return(100u));
    addSingleUtxo(4*COIN,scripts[0]);
    addSingleUtxo(2*COIN+1500,scripts[1]);
    addSingleUtxo(2*COIN+1000,scripts[2]);

    std::vector<COutput> utxos = getSpendableOutputs();
    CAmount toSpend = 5*COIN;
    CMutableTransaction novelTx;
    novelTx.vout.emplace_back(toSpend,RandomCScriptGenerator()(25u));
    CAmount feesPaidEstimate = 1*COIN;
    std::set<COutput> selectedUTXOs = algorithm.SelectCoins(novelTx,utxos,feesPaidEstimate);
    BOOST_CHECK_MESSAGE(feesPaidEstimate > 1*COIN,"Initial fee estimate not covered");
}

BOOST_AUTO_TEST_CASE(someNewTest)
{
    std::vector<CScript> scripts = { scriptGenerator(25),scriptGenerator(25),scriptGenerator(25)};
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,scripts[0])).WillByDefault(Return(240u));
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,scripts[1])).WillByDefault(Return(240u));
    ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,scripts[2])).WillByDefault(Return(240u));
    addSingleUtxo(2*COIN,scripts[0]);
    addSingleUtxo(2*COIN,scripts[1]);
    addSingleUtxo(2*COIN,scripts[2]);

    std::vector<COutput> utxos = getSpendableOutputs();
    CAmount toSpend = 4*COIN-5460;
    CMutableTransaction novelTx;
    novelTx.vout.emplace_back(toSpend,RandomCScriptGenerator()(25u));
    CAmount feesPaidEstimate = 0;
    std::set<COutput> selectedUTXOs = algorithm.SelectCoins(novelTx,utxos,feesPaidEstimate);
    CAmount totalInputs = 0;
    for(const COutput& spentAsInput: selectedUTXOs)
    {
        totalInputs += spentAsInput.Value();
    }
    BOOST_CHECK_MESSAGE(totalInputs > 0, "Unable to select enough coins");
    BOOST_CHECK_MESSAGE(totalInputs == 6*COIN, "Spent less than all coins");
    BOOST_CHECK_MESSAGE(totalInputs >= toSpend + feesPaidEstimate, "Inputs less than intended expenditure");
}

BOOST_AUTO_TEST_CASE(willNotPayMoreInFeesThanMaximumSet)
{
    std::vector<CScript> scripts = { scriptGenerator(25),scriptGenerator(25),scriptGenerator(25)};
    std::vector<CAmount> amounts = {4*COIN,2*COIN+1500,2*COIN+1000};
    std::vector<unsigned> sigSizes = {100u,150u,100u};

    CAmount totalBalance = 0;
    for(unsigned utxoIndex = 0; utxoIndex < 3; ++utxoIndex)
    {
        ON_CALL(mockSignatureSizeEstimator,MaxBytesNeededForSigning(_,scripts[utxoIndex])).WillByDefault(Return(sigSizes[utxoIndex]));
        addSingleUtxo(amounts[utxoIndex],scripts[0]);
        totalBalance += amounts[utxoIndex];
    }

    std::vector<COutput> utxos = getSpendableOutputs();
    CAmount toSpend = 5*COIN;
    CMutableTransaction novelTx;
    novelTx.vout.emplace_back(toSpend,RandomCScriptGenerator()(25u));

    CAmount feesPaidEstimate = 0*COIN;
    std::set<COutput> selectedUTXOsWithoutFeeRestrictions = algorithm.SelectCoins(novelTx,utxos,feesPaidEstimate);
    BOOST_CHECK_MESSAGE(!selectedUTXOsWithoutFeeRestrictions.empty(),"Unable to select utxos!");

    feeRate.SetMaxFee(feesPaidEstimate-1);
    CAmount newFeesPaidEstimate = 0*COIN;
    std::set<COutput> selectedUTXOsWithFeeRestrictions = algorithm.SelectCoins(novelTx,utxos,newFeesPaidEstimate);
    BOOST_CHECK_MESSAGE(selectedUTXOsWithFeeRestrictions.empty(),"Paid in excess of maximum fees!");
}

BOOST_AUTO_TEST_CASE(willEstimateFeesPaidByExistingInputs)
{
    unsigned numberOfUtxos = GetRandInt(10)+1;
    for(unsigned numberOfUtxosToAdd = numberOfUtxos; numberOfUtxosToAdd > 0; --numberOfUtxosToAdd)
    {
        CTxOut utxo = RandomUtxoGenerator()(100);
        addSingleUtxo(utxo.nValue+1,utxo.scriptPubKey);
    }
    CMutableTransaction nextTransaction;
    const auto utxos = getSpendableOutputs();
    for(const COutput& output: utxos)
    {
        nextTransaction.vin.emplace_back(output.tx->GetHash(),output.i);
    }
    CAmount toSpend = 1*COIN;
    nextTransaction.vout.emplace_back(toSpend,RandomCScriptGenerator()(25u));
    CTransaction finalizedTransaction(nextTransaction);
    unsigned totalSigSizes = utxos.size()*40; // 32-bytes for hash, 8 bytes for output index
    unsigned txSize = ::GetSerializeSize(finalizedTransaction, SER_NETWORK, PROTOCOL_VERSION) + totalSigSizes;

    CAmount feesPaidEstimate = 0*COIN;
    std::set<COutput> selectedUTXOsWithoutFeeRestrictions = algorithm.SelectCoins(nextTransaction,utxos,feesPaidEstimate);
    std::cout << "This is a test: " << numberOfUtxos << ", " << utxos.size() << std::endl;
    BOOST_CHECK_EQUAL_MESSAGE(feesPaidEstimate, feeRate.GetFee(txSize), "Fee estimated and fee rate mismatched");

}
BOOST_AUTO_TEST_SUITE_END()