#include <test/test_only.h>
#include <MinimumFeeCoinSelectionAlgorithm.h>
#include <keystore.h>
#include <key.h>
#include <script/standard.h>
#include <RandomCScriptGenerator.h>
#include <vector>
#include <set>
#include <WalletTx.h>
#include <test/MockSignatureSizeEstimator.h>

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
    std::vector<CWalletTx> walletTransactions_;
public:
    std::vector<CKeyID> keyIds;
    NiceMock<MockSignatureSizeEstimator> mockSignatureSizeEstimator;
    MinimumFeeCoinSelectionAlgorithm algorithm;
    RandomCScriptGenerator scriptGenerator;
    CScript smallScriptSigReqScript;
    CScript largeScriptSigReqScript;

    MinimumFeeCoinSelectionAlgorithmTestFixture(
        ): keystore_()
        , walletTransactions_()
        , keyIds()
        , mockSignatureSizeEstimator()
        , algorithm(keystore_,mockSignatureSizeEstimator)
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

BOOST_AUTO_TEST_SUITE_END()