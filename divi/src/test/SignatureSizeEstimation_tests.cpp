#include <test_only.h>

#include <key.h>
#include <pubkey.h>
#include <primitives/transaction.h>
#include <keystore.h>
#include <SignatureSizeEstimator.h>
#include <destination.h>
#include <script/standard.h>
#include <script/sign.h>

class SignatureSizeTestFixture
{
private:
    std::vector<CKey> keys_;
    CBasicKeyStore keyStore_;
public:
    SignatureSizeTestFixture(
        ): keys_()
        , keyStore_()
    {
    }
    void createKeys(unsigned numberOfKeys, bool compressedKey = true)
    {
        keys_.resize(numberOfKeys);
        for(CKey& key: keys_)
        {
            key.MakeNewKey(compressedKey);
        }
    }
    void addSingleKey(bool compressedKey = true)
    {
        keys_.emplace_back();
        keys_.back().MakeNewKey(compressedKey);
    }
    bool addKeyToStoreByIndex(unsigned keyIndex)
    {
        if(!(keyIndex< keys_.size()) ) return false;
        return keyStore_.AddKey(keys_[keyIndex]);
    }
    void addKeysToStore()
    {
        for(CKey& key: keys_)
        {
            keyStore_.AddKey(key);
        }
    }
    CKey& getKeyByIndex(unsigned keyIndex)
    {
        assert(keyIndex < keys_.size());
        return keys_[keyIndex];
    }
    CBasicKeyStore& getKeyStore()
    {
        return keyStore_;
    }
};

BOOST_FIXTURE_TEST_SUITE(SignatureSizeEstimationTests, SignatureSizeTestFixture)

BOOST_AUTO_TEST_CASE(willDefaultToALargeByteSizeWhenScriptIsEmpty)
{
    BOOST_CHECK_EQUAL(
        SignatureSizeEstimator::MaxBytesNeededForSigning(getKeyStore(),CScript()),
        std::numeric_limits<unsigned>::max());
}
BOOST_AUTO_TEST_CASE(willDefaultToALargeByteSizeWhenScriptIsNonStandard)
{
    CScript nonstandardScript = CScript() << OP_FALSE << OP_DROP << OP_TRUE;
    BOOST_CHECK_EQUAL(
        SignatureSizeEstimator::MaxBytesNeededForSigning(getKeyStore(),nonstandardScript),
        std::numeric_limits<unsigned>::max());
}
BOOST_AUTO_TEST_CASE(willDefaultToALargestByteSizePossibleWhenKeyIsUnknown)
{
    createKeys(2);
    addKeyToStoreByIndex(0);
    CKey& unknownKey = getKeyByIndex(1);
    CScript alternateScript = GetScriptForDestination(unknownKey.GetPubKey().GetID());
    BOOST_CHECK_EQUAL(
        SignatureSizeEstimator::MaxBytesNeededForSigning(getKeyStore(),alternateScript), 139u);
}
BOOST_AUTO_TEST_CASE(willRecoverCorrectSignatureSizeForP2PKHScriptsWhenKeyIsKnown)
{
    addSingleKey(false);
    addSingleKey(true);

    for(unsigned keyIndex =0 ; keyIndex < 2; keyIndex++)
    {
        addKeyToStoreByIndex(keyIndex);
        CKey& knownKey = getKeyByIndex(keyIndex);
        CPubKey knownPubKey = knownKey.GetPubKey();
        CScript knownScript = GetScriptForDestination(knownPubKey.GetID());
        CMutableTransaction sampleTransaction;
        sampleTransaction.vin.emplace_back(uint256S("0x8b4bdd6fd8220ca956938d214cbd4635bfaacc663f53ad8bda5e434b9dc647fe"),1);

        const unsigned maximumBytesEstimate = SignatureSizeEstimator::MaxBytesNeededForSigning(getKeyStore(),knownScript);
        const unsigned initialTxSize = ::GetSerializeSize(CTransaction(sampleTransaction),SER_NETWORK, PROTOCOL_VERSION);
        BOOST_CHECK(SignSignature(getKeyStore(), knownScript, sampleTransaction, 0, SIGHASH_ALL));
        const unsigned postSignatureTxSize = ::GetSerializeSize(CTransaction(sampleTransaction),SER_NETWORK, PROTOCOL_VERSION);

        BOOST_CHECK_MESSAGE(postSignatureTxSize-initialTxSize <= maximumBytesEstimate,"scriptSig size is above expected!");
        BOOST_CHECK_MESSAGE(postSignatureTxSize-initialTxSize >= maximumBytesEstimate -1,"scriptSig size is below expected!");
    }
}

BOOST_AUTO_TEST_CASE(willRecoverCorrectSignatureSizeForP2PKScriptsWhenKeyIsKnown)
{
    addSingleKey(false);
    addSingleKey(true);

    for(unsigned keyIndex =0 ; keyIndex < 2; keyIndex++)
    {
        addKeyToStoreByIndex(keyIndex);
        CKey& knownKey = getKeyByIndex(keyIndex);
        CPubKey knownPubKey = knownKey.GetPubKey();
        CScript knownScript = CScript() << ToByteVector(knownPubKey) << OP_CHECKSIG;
        CMutableTransaction sampleTransaction;
        sampleTransaction.vin.emplace_back(uint256S("0x8b4bdd6fd8220ca956938d214cbd4635bfaacc663f53ad8bda5e434b9dc647fe"),1);

        const unsigned maximumBytesEstimate = SignatureSizeEstimator::MaxBytesNeededForSigning(getKeyStore(),knownScript);
        const unsigned initialTxSize = ::GetSerializeSize(CTransaction(sampleTransaction),SER_NETWORK, PROTOCOL_VERSION);
        BOOST_CHECK(SignSignature(getKeyStore(), knownScript, sampleTransaction, 0, SIGHASH_ALL));
        const unsigned postSignatureTxSize = ::GetSerializeSize(CTransaction(sampleTransaction),SER_NETWORK, PROTOCOL_VERSION);

        const unsigned changeInByteCount = postSignatureTxSize-initialTxSize;
        BOOST_CHECK_MESSAGE(changeInByteCount <= maximumBytesEstimate,"scriptSig size is above expected! "+std::to_string(changeInByteCount));
        BOOST_CHECK_MESSAGE(changeInByteCount >= maximumBytesEstimate -1,"scriptSig size is below expected!"+std::to_string(changeInByteCount));
    }
}

BOOST_AUTO_TEST_SUITE_END()