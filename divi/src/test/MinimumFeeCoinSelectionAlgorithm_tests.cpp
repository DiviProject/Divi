#include <test/test_only.h>
#include <MinimumFeeCoinSelectionAlgorithm.h>
#include <keystore.h>
#include <key.h>
#include <script/standard.h>
#include <RandomCScriptGenerator.h>
#include <vector>
#include <set>
#include <WalletTx.h>

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
    MinimumFeeCoinSelectionAlgorithm algorithm;
    CScript compressedKeyScript;
    CScript uncompressedKeyScript;

    MinimumFeeCoinSelectionAlgorithmTestFixture(
        ): keystore_()
        , walletTransactions_()
        , keyIds()
        , algorithm(keystore_)
        , compressedKeyScript()
        , uncompressedKeyScript()
    {
        compressedKeyScript = addSingleKey(true);
        uncompressedKeyScript = addSingleKey(false);
    }
    void addSingleUtxo(const CAmount utxoAmount, bool compressedKey)
    {
        static RandomCScriptGenerator scriptGenerator;
        CMutableTransaction mutableTx;
        mutableTx.vin.emplace_back();
        mutableTx.vin[0].scriptSig = scriptGenerator(25);
        mutableTx.vout.emplace_back(utxoAmount,compressedKey?compressedKeyScript:uncompressedKeyScript);
        walletTransactions_.emplace_back(CTransaction(mutableTx));
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

BOOST_AUTO_TEST_CASE(willPreferentiallyUseCompressedKeyUTXOS)
{
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
            outputSelected.scriptPubKey()==compressedKeyScript,
            "Unexpected output being used. Paying more in fees than necessary");
    }
}
BOOST_AUTO_TEST_CASE(willPreferentiallyUseSingleUTXOsThatPayLessInFees)
{
    addSingleUtxo(1*COIN + 32*CENT/100 + 1,false);
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
            outputSelected.scriptPubKey()==compressedKeyScript,
            "Unexpected output being used. Paying more in fees than necessary");
    }
}

BOOST_AUTO_TEST_SUITE_END()