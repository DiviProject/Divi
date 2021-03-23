#include <test_only.h>

#include <key.h>
#include <pubkey.h>
#include <primitives/transaction.h>
#include <keystore.h>
#include <SignatureSizeEstimator.h>
#include <destination.h>
#include <script/standard.h>

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

BOOST_AUTO_TEST_SUITE_END()