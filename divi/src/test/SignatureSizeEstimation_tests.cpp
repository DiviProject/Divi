#include <test_only.h>

#include <key.h>
#include <pubkey.h>
#include <primitives/transaction.h>
#include <keystore.h>
#include <SignatureSizeEstimator.h>
#include <destination.h>
#include <script/standard.h>
#include <script/sign.h>
#include <map>
#include <script/StakingVaultScript.h>

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

    CMutableTransaction getSampleTransaction()
    {
        CMutableTransaction sampleTransaction;
        sampleTransaction.vin.emplace_back(uint256S("0x8b4bdd6fd8220ca956938d214cbd4635bfaacc663f53ad8bda5e434b9dc647fe"),1);
        return sampleTransaction;
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

    CScript getMultisigScriptFromKeys(unsigned requiredSigs) const
    {
        assert(keys_.size() >= requiredSigs);
        std::vector<CPubKey> pubkeys;
        pubkeys.reserve(keys_.size());
        for(const CKey& key: keys_)
        {
            pubkeys.push_back(key.GetPubKey());
        }
        return GetScriptForMultisig(requiredSigs,pubkeys);
    }

    void addScriptToKeyStore(const CScript& script)
    {
        keyStore_.AddCScript(script);
    }
    CScript getVaultScriptFromKeys()
    {
        return CreateStakingVaultScript(
            ToByteVector(keys_[0].GetPubKey().GetID()),
            ToByteVector(keys_[1].GetPubKey().GetID()));
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
        SignatureSizeEstimator::MaxBytesNeededForSigning(getKeyStore(),alternateScript), 140u);
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
        CMutableTransaction sampleTransaction = getSampleTransaction();

        const unsigned maximumBytesEstimate = SignatureSizeEstimator::MaxBytesNeededForSigning(getKeyStore(),knownScript);
        const unsigned initialTxSize = ::GetSerializeSize(CTransaction(sampleTransaction),SER_NETWORK, PROTOCOL_VERSION);
        BOOST_CHECK(SignSignature(getKeyStore(), knownScript, sampleTransaction, 0, SIGHASH_ALL));
        const unsigned postSignatureTxSize = ::GetSerializeSize(CTransaction(sampleTransaction),SER_NETWORK, PROTOCOL_VERSION);

        const unsigned changeInByteCount = postSignatureTxSize-initialTxSize;
        BOOST_CHECK_MESSAGE(changeInByteCount <= maximumBytesEstimate,
            "scriptSig size is above expected! "+std::to_string(changeInByteCount)+
            " vs "+std::to_string(maximumBytesEstimate));
        BOOST_CHECK_MESSAGE(changeInByteCount >= maximumBytesEstimate -3,
            "scriptSig size is below expected!"+std::to_string(changeInByteCount)+
            " vs "+std::to_string(maximumBytesEstimate-2u));
    }
}

BOOST_AUTO_TEST_CASE(willRecoverCorrectSignatureSizeForP2PKScriptsWhenKeyIsKnown)
{
    addSingleKey(false);
    addSingleKey(true);

    for(unsigned keyIndex = 0u ; keyIndex < 2u; keyIndex++)
    {
        addKeyToStoreByIndex(keyIndex);
        CKey& knownKey = getKeyByIndex(keyIndex);
        CPubKey knownPubKey = knownKey.GetPubKey();
        CScript knownScript = CScript() << ToByteVector(knownPubKey) << OP_CHECKSIG;
        CMutableTransaction sampleTransaction = getSampleTransaction();

        const unsigned maximumBytesEstimate = SignatureSizeEstimator::MaxBytesNeededForSigning(getKeyStore(),knownScript);
        const unsigned initialTxSize = ::GetSerializeSize(CTransaction(sampleTransaction),SER_NETWORK, PROTOCOL_VERSION);
        BOOST_CHECK(SignSignature(getKeyStore(), knownScript, sampleTransaction, 0, SIGHASH_ALL));
        const unsigned postSignatureTxSize = ::GetSerializeSize(CTransaction(sampleTransaction),SER_NETWORK, PROTOCOL_VERSION);

        const unsigned changeInByteCount = postSignatureTxSize-initialTxSize;
        BOOST_CHECK_MESSAGE(changeInByteCount <= maximumBytesEstimate,
            "scriptSig size is above expected! "+std::to_string(changeInByteCount)+
            " vs "+std::to_string(maximumBytesEstimate));
        BOOST_CHECK_MESSAGE(changeInByteCount >= maximumBytesEstimate -3,
            "scriptSig size is below expected!"+std::to_string(changeInByteCount)+
            " vs "+std::to_string(maximumBytesEstimate-3u));
    }
}

BOOST_AUTO_TEST_CASE(willRecoverCorrectSignatureSizeForMultiSigScriptsWhenKeysAreKnown)
{
    createKeys(10);
    unsigned addedKeyIndex = 0;
    for(unsigned requiredKeys = 1u; requiredKeys < 10; requiredKeys++)
    {
        addKeyToStoreByIndex(addedKeyIndex++);
        CScript knownScript = getMultisigScriptFromKeys(requiredKeys);
        CMutableTransaction sampleTransaction = getSampleTransaction();

        const unsigned maximumBytesEstimate = SignatureSizeEstimator::MaxBytesNeededForSigning(getKeyStore(),knownScript);
        const unsigned initialTxSize = ::GetSerializeSize(CTransaction(sampleTransaction),SER_NETWORK, PROTOCOL_VERSION);
        BOOST_CHECK(SignSignature(getKeyStore(), knownScript, sampleTransaction, 0, SIGHASH_ALL));
        const unsigned postSignatureTxSize = ::GetSerializeSize(CTransaction(sampleTransaction),SER_NETWORK, PROTOCOL_VERSION);

        const unsigned changeInByteCount = postSignatureTxSize-initialTxSize;
        BOOST_CHECK_MESSAGE(
            changeInByteCount <= maximumBytesEstimate,
            "scriptSig size is above expected! "+std::to_string(changeInByteCount)+" while expecting "+std::to_string(requiredKeys)+" keys");
        BOOST_CHECK_MESSAGE(
            changeInByteCount >= maximumBytesEstimate - 3*requiredKeys,
            "scriptSig size is below expected!"+std::to_string(changeInByteCount)+" while expecting "+std::to_string(requiredKeys)+" keys");
    }
}

BOOST_AUTO_TEST_CASE(willRecoverCorrectSignatureSizeForVaultScriptsWhenKeysAreKnown)
{
    createKeys(2);
    CScript knownScript = getVaultScriptFromKeys();

    for(unsigned keyIndex = 1u ; keyIndex-- > 0u; )
    {
        addKeyToStoreByIndex(keyIndex);
        CMutableTransaction sampleTransaction = getSampleTransaction();

        const unsigned maximumBytesEstimate = SignatureSizeEstimator::MaxBytesNeededForSigning(getKeyStore(),knownScript);
        const unsigned initialTxSize = ::GetSerializeSize(CTransaction(sampleTransaction),SER_NETWORK, PROTOCOL_VERSION);
        BOOST_CHECK(SignSignature(getKeyStore(), knownScript, sampleTransaction, 0, SIGHASH_ALL));
        const unsigned postSignatureTxSize = ::GetSerializeSize(CTransaction(sampleTransaction),SER_NETWORK, PROTOCOL_VERSION);

        const unsigned changeInByteCount = postSignatureTxSize-initialTxSize;
        BOOST_CHECK_MESSAGE(
            changeInByteCount <= maximumBytesEstimate,
            "scriptSig size is above expected! "+std::to_string(changeInByteCount));
        BOOST_CHECK_MESSAGE(
            changeInByteCount >= maximumBytesEstimate - 3,
            "scriptSig size is below expected!"+std::to_string(changeInByteCount));
    }

}

BOOST_AUTO_TEST_CASE(willRecoverCorrectSignatureSizeForP2SHScriptsWhenScriptsAreKnown)
{
    const unsigned maxNumberOfKeysUsed = 5;
    createKeys(maxNumberOfKeysUsed);
    addKeysToStore();
    CScript p2pk = CScript() << ToByteVector(getKeyByIndex(0u).GetPubKey()) << OP_CHECKSIG;
    CScript p2pkh = GetScriptForDestination(getKeyByIndex(1u).GetPubKey().GetID());
    CScript multiSig = getMultisigScriptFromKeys(5);

    std::map<CScript,CScript> p2shScripts ={
        {p2pk,CScript() << OP_HASH160 << ToByteVector(CScriptID(p2pk)) << OP_EQUAL},
        {p2pkh,CScript() << OP_HASH160 << ToByteVector(CScriptID(p2pkh)) << OP_EQUAL},
        {multiSig,CScript() << OP_HASH160 << ToByteVector(CScriptID(multiSig)) << OP_EQUAL}};

    for(const auto& scriptAndP2SHScript: p2shScripts)
    {
        addScriptToKeyStore(scriptAndP2SHScript.first);
        const CScript& knownP2SHScript = scriptAndP2SHScript.second;
        CMutableTransaction sampleTransaction = getSampleTransaction();

        const unsigned maximumBytesEstimate = SignatureSizeEstimator::MaxBytesNeededForSigning(getKeyStore(),knownP2SHScript);
        const unsigned initialTxSize = ::GetSerializeSize(CTransaction(sampleTransaction),SER_NETWORK, PROTOCOL_VERSION);
        BOOST_CHECK(SignSignature(getKeyStore(), knownP2SHScript, sampleTransaction, 0, SIGHASH_ALL));
        const unsigned postSignatureTxSize = ::GetSerializeSize(CTransaction(sampleTransaction),SER_NETWORK, PROTOCOL_VERSION);
        const unsigned changeInByteCount = postSignatureTxSize-initialTxSize;
        BOOST_CHECK_MESSAGE(
            changeInByteCount <= maximumBytesEstimate,
            "scriptSig size is above expected! "+std::to_string(changeInByteCount)+" while expecting around "+std::to_string(maximumBytesEstimate)+" bytes");
        BOOST_CHECK_MESSAGE(
            changeInByteCount >= maximumBytesEstimate - 3*maxNumberOfKeysUsed,
            "scriptSig size is below expected!"+std::to_string(changeInByteCount)+" while expecting around "+std::to_string(maximumBytesEstimate)+" bytes");
    }
}


BOOST_AUTO_TEST_SUITE_END()