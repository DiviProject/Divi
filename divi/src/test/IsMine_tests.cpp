#include <wallet_ismine.h>
#include <keystore.h>
#include <script/script.h>
#include <script/StakingVaultScript.h>
#include <base58.h>
#include <string>

#include "test_only.h"


class TestKeys
{
public:
    std::vector<CKey> keys_;
    std::vector<CPubKey> pubkeys_;

    TestKeys(bool onlyCompressedKeys = true): keys_(), pubkeys_()
    {
        keys_.resize(4);
        for(CKey& key: keys_)
        {
            key.MakeNewKey(onlyCompressedKeys);
        }
        pubkeys_.reserve(4);
        for(const CKey& key: keys_)
        {
            pubkeys_.push_back(key.GetPubKey());
        }
    }
};


class CScriptCreator
{
public:
    CScriptCreator(){}

    static CScript P2PK(const CPubKey& pubkey)
    {
        CScript script;
        script.clear();
        script << ToByteVector(pubkey) << OP_CHECKSIG;
        return script;
    }
    static CScript P2PKH(const CKeyID &keyID)
    {
        CScript script;
        script.clear();
        script << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
        return script;
    }

    static CScript P2SH(const CScriptID &scriptID)
    {
        CScript script;
        script.clear();
        script << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
        return script;
    }
    static CScript MultiSig(const std::vector<CPubKey>& pubkeys, int requiredSigs)
    {
        int totalSignatories = static_cast<int>(pubkeys.size());
        assert(requiredSigs >= 0 && requiredSigs <= 16);
        assert(totalSignatories >= 0 && totalSignatories <= 16);
        CScript script;
        script.clear();
        script <<  CScript::EncodeOP_N(requiredSigs);
        for(const CPubKey& pubkey: pubkeys)
        {
            script << ToByteVector(pubkey);
        }
        script << CScript::EncodeOP_N(totalSignatories) << OP_CHECKMULTISIG;
        return script;
    }
    static CScript Vault(const CKeyID& ownerID, const CKeyID& managerID)
    {
        return CreateStakingVaultScript(ToByteVector(ownerID),ToByteVector(managerID));
    }
};

struct FixedScripts
{
    TestKeys keyChain;
    const CScript p2pk;
    const CScript p2pkh;
    const CScript multisig;
    const CScript nonstandard;
    const CScript nulldata;
    const CScript vault;
    const CScript vaultAsManager;
    std::vector<CScript> allScriptTypes;
    std::vector<std::vector<CKey>> neededKeys;

    FixedScripts(
        ): keyChain()
        , p2pk(CScriptCreator::P2PK(keyChain.pubkeys_[0]))
        , p2pkh(CScriptCreator::P2PKH(keyChain.pubkeys_[0].GetID()))
        , multisig(CScriptCreator::MultiSig(keyChain.pubkeys_,keyChain.pubkeys_.size()))
        , nonstandard(CScript() << OP_TRUE << OP_DROP << OP_FALSE)
        , nulldata(CScript() << OP_META << ToByteVector(std::string("Some random message")) )
        , vault(CScriptCreator::Vault(keyChain.pubkeys_[0].GetID(),keyChain.pubkeys_[1].GetID()))
        , vaultAsManager(CScriptCreator::Vault(keyChain.pubkeys_[2].GetID(),keyChain.pubkeys_[1].GetID()))
        , allScriptTypes()
        , neededKeys()
    {
        neededKeys.reserve(7);
        allScriptTypes.reserve(7);

        allScriptTypes.push_back(p2pk);
        neededKeys.emplace_back( std::vector<CKey>({keyChain.keys_[0]})  );

        allScriptTypes.push_back(p2pkh);
        neededKeys.emplace_back( std::vector<CKey>({keyChain.keys_[0]})  );

        allScriptTypes.push_back(multisig);
        neededKeys.emplace_back( keyChain.keys_  );

        allScriptTypes.push_back(nonstandard);
        neededKeys.emplace_back();

        allScriptTypes.push_back(nulldata);
        neededKeys.emplace_back();

        allScriptTypes.push_back(vault);
        neededKeys.emplace_back(std::vector<CKey>({keyChain.keys_[0]}));

        allScriptTypes.push_back(vaultAsManager);
        neededKeys.emplace_back(std::vector<CKey>({keyChain.keys_[1]}));
    }
};

BOOST_AUTO_TEST_SUITE(IsMineTests)

BOOST_AUTO_TEST_CASE(willNotOwnAnyScriptsWhenKeyStoreIsEmpty)
{
    FixedScripts fixedScripts;
    CBasicKeyStore keyStore;
    for(const CScript& candidateScript: fixedScripts.allScriptTypes)
    {
        BOOST_CHECK_MESSAGE(IsMine(keyStore, candidateScript)==ISMINE_NO, "Asserted ownership over unknown funds");
    }
}
BOOST_AUTO_TEST_CASE(willFailToOwnNonStandardScriptOrDataPushScriptsUnlessWatching)
{
    FixedScripts fixedScripts;
    CBasicKeyStore keyStore;
    for(const CKey& key: fixedScripts.keyChain.keys_)
    {
        keyStore.AddKey(key);
    }
    BOOST_CHECK_MESSAGE(IsMine(keyStore, fixedScripts.nulldata)==ISMINE_NO, "Asserted ownership over data push script");
    BOOST_CHECK_MESSAGE(IsMine(keyStore, fixedScripts.nonstandard)==ISMINE_NO, "Asserted ownership over nonstandard script");
    {
        keyStore.AddWatchOnly(fixedScripts.nulldata);
        BOOST_CHECK_MESSAGE(IsMine(keyStore, fixedScripts.nulldata)==ISMINE_WATCH_ONLY, "Could not find watched data script!");

        keyStore.AddWatchOnly(fixedScripts.nonstandard);
        BOOST_CHECK_MESSAGE(IsMine(keyStore, fixedScripts.nonstandard)==ISMINE_WATCH_ONLY, "Could not find watched nonstandard script!");
    }
}

BOOST_AUTO_TEST_CASE(willDetectScriptsAreOwnedWhenTheNeededKeysAreAvailable)
{
    FixedScripts fixedScripts;
    for(unsigned testCaseIndex = 0; testCaseIndex < fixedScripts.allScriptTypes.size(); ++testCaseIndex)
    {
        CBasicKeyStore keyStore;
        for(const CKey& key: fixedScripts.neededKeys[testCaseIndex])
        {
            keyStore.AddKey(key);
        }
        if(fixedScripts.allScriptTypes[testCaseIndex] != fixedScripts.nulldata &&
            fixedScripts.allScriptTypes[testCaseIndex] != fixedScripts.nonstandard &&
            fixedScripts.allScriptTypes[testCaseIndex] != fixedScripts.vaultAsManager)
        {
            auto result = IsMine(keyStore, fixedScripts.allScriptTypes[testCaseIndex]);
            BOOST_CHECK_MESSAGE(result!=ISMINE_NO,
                "Did not find script to be owned even with all keys held: "+std::to_string(testCaseIndex)+ " With output: "+ std::to_string(result));
        }
    }
}

BOOST_AUTO_TEST_CASE(willDetectWhenVaultIsOwnedOrManagedAndDefaultToOwnedWhenBoth)
{
    TestKeys keychain;
    CScript vaultScript = CScriptCreator::Vault(keychain.pubkeys_[0].GetID(),keychain.pubkeys_[1].GetID());
    {
        CBasicKeyStore keyStore;
        keyStore.AddKey(keychain.keys_[0]);

        VaultType vaultOwnershipType;
        IsMine(keyStore,vaultScript,vaultOwnershipType);
        BOOST_CHECK_MESSAGE(vaultOwnershipType == OWNED_VAULT, "Did not detect owned vault");
    }
    {
        CBasicKeyStore keyStore;
        keyStore.AddKey(keychain.keys_[1]);

        VaultType vaultOwnershipType;
        IsMine(keyStore,vaultScript,vaultOwnershipType);
        BOOST_CHECK_MESSAGE(vaultOwnershipType == MANAGED_VAULT, "Did not detect managed vault for which responsability wasnt accepted");
    }
    {
        CBasicKeyStore keyStore;
        keyStore.AddKey(keychain.keys_[1]);
        keyStore.AddCScript(vaultScript);

        VaultType vaultOwnershipType;
        IsMine(keyStore,vaultScript,vaultOwnershipType);
        BOOST_CHECK_MESSAGE(vaultOwnershipType == MANAGED_VAULT, "Did not detect managed vault");
    }
    {
        CBasicKeyStore keyStore;
        keyStore.AddKey(keychain.keys_[0]);
        keyStore.AddKey(keychain.keys_[1]);

        VaultType vaultOwnershipType;
        IsMine(keyStore,vaultScript,vaultOwnershipType);
        BOOST_CHECK_MESSAGE(vaultOwnershipType == OWNED_VAULT, "Did not default to owned when vault is both owned and managed");
    }
}

BOOST_AUTO_TEST_CASE(willFailToOwnAManagedVaultForWhichResponsibilityHasNotBeenAccepted)
{
    TestKeys keychain;
    CScript vaultScript = CScriptCreator::Vault(keychain.pubkeys_[0].GetID(),keychain.pubkeys_[1].GetID());
    CBasicKeyStore keyStore;
    keyStore.AddKey(keychain.keys_[1]);

    VaultType vaultOwnershipType;
    BOOST_CHECK_MESSAGE( IsMine(keyStore,vaultScript,vaultOwnershipType) == ISMINE_NO, "Detected managed vault for which responsability wasnt accepted");
}
BOOST_AUTO_TEST_CASE(willOwnAManagedVaultForWhichResponsibilityHasBeenAccepted)
{
    TestKeys keychain;
    CScript vaultScript = CScriptCreator::Vault(keychain.pubkeys_[0].GetID(),keychain.pubkeys_[1].GetID());
    CBasicKeyStore keyStore;
    keyStore.AddKey(keychain.keys_[1]);
    keyStore.AddCScript(vaultScript);

    VaultType vaultOwnershipType;
    BOOST_CHECK_MESSAGE( IsMine(keyStore,vaultScript,vaultOwnershipType) == ISMINE_SPENDABLE, "Did not detect managed vault");
}

BOOST_AUTO_TEST_CASE(willDetectThatP2PKScriptsAreNotVaults)
{
    TestKeys keychain;
    CBasicKeyStore keyStore;
    for(const CKey& key: keychain.keys_)
    {
        keyStore.AddKey(key);
    }

    for(const CPubKey& pubkey: keychain.pubkeys_)
    {
        CScript someNonVaultScript = CScriptCreator::P2PK(pubkey);
        VaultType vaultOwnershipType;
        IsMine(keyStore,someNonVaultScript,vaultOwnershipType);
        BOOST_CHECK_MESSAGE(vaultOwnershipType == NON_VAULT, "Did not detect non vault: p2pk");
    }
}

BOOST_AUTO_TEST_CASE(willDetectThatP2PKHScriptsAreNotVaults)
{
    TestKeys keychain;
    CBasicKeyStore keyStore;
    for(const CKey& key: keychain.keys_)
    {
        keyStore.AddKey(key);
    }
    for(const CPubKey& pubkey: keychain.pubkeys_)
    {
        CScript someNonVaultScript = CScriptCreator::P2PKH(pubkey.GetID());
        VaultType vaultOwnershipType;
        IsMine(keyStore,someNonVaultScript,vaultOwnershipType);
        BOOST_CHECK_MESSAGE(vaultOwnershipType == NON_VAULT, "Did not detect non vault: p2pkh");
    }
}

BOOST_AUTO_TEST_CASE(willDetectThatMultiSigScriptsArentVaults)
{
    TestKeys keychain;
    CBasicKeyStore keyStore;
    for(const CKey& key: keychain.keys_)
    {
        keyStore.AddKey(key);
    }

    {
        CScript someNonVaultScript = CScriptCreator::MultiSig(keychain.pubkeys_,4);
        VaultType vaultOwnershipType;
        IsMine(keyStore,someNonVaultScript,vaultOwnershipType);
        BOOST_CHECK_MESSAGE(vaultOwnershipType == NON_VAULT,
            "Did not detect non vault: Have needed keys but keystore has not recorded this multisig");
    }
    {
        CScript someNonVaultScript = CScriptCreator::MultiSig(keychain.pubkeys_,4);

        keyStore.AddMultiSig(someNonVaultScript);
        VaultType vaultOwnershipType;
        IsMine(keyStore,someNonVaultScript,vaultOwnershipType);
        BOOST_CHECK_MESSAGE(vaultOwnershipType == NON_VAULT,
            "Did not detect non vault: Have needed keys and keystore is expecting this multisig");
    }
}

BOOST_AUTO_TEST_CASE(willDetectNonStandardAndDataPushScriptsArentVaults)
{
    CBasicKeyStore keyStore;
    { //Non Standard
        CScript sourceScript = CScript() << OP_TRUE << OP_DROP << OP_FALSE;
        VaultType vaultOwnershipType;
        IsMine(keyStore,sourceScript,vaultOwnershipType);
        BOOST_CHECK_MESSAGE(vaultOwnershipType == NON_VAULT, "Did not detect non vault: non standard tx");
    }
    { // Null Data
        CScript sourceScript = CScript() << OP_META << ToByteVector(std::string("Some random message"));
        CScript someNonVaultScript = CScriptCreator::P2SH(CScriptID(sourceScript));

        keyStore.AddCScript(sourceScript);
        VaultType vaultOwnershipType;
        IsMine(keyStore,someNonVaultScript,vaultOwnershipType);
        BOOST_CHECK_MESSAGE(vaultOwnershipType == NON_VAULT, "Did not detect non vault: data push");
    }
}

BOOST_AUTO_TEST_CASE(willDetectWhichP2SHScriptsAreVaultsOrNot)
{
    TestKeys keyChain;
    {
        CBasicKeyStore keyStore;
        keyStore.AddKey(keyChain.keys_[0]);
        CScript sourceScript = CScriptCreator::P2PKH(keyChain.pubkeys_[0].GetID());
        CScript someNonVaultScript = CScriptCreator::P2SH(CScriptID(sourceScript));

        keyStore.AddCScript(sourceScript);
        VaultType vaultOwnershipType;
        IsMine(keyStore,someNonVaultScript,vaultOwnershipType);
        BOOST_CHECK_MESSAGE(vaultOwnershipType == NON_VAULT, "Did not detect non vault: p2sh - wrapping p2pkh");
    }
    {
        CBasicKeyStore keyStore;
        keyStore.AddKey(keyChain.keys_[0]);
        CScript sourceScript = CScriptCreator::Vault(keyChain.pubkeys_[0].GetID(),keyChain.pubkeys_[1].GetID());
        CScript someNonVaultScript = CScriptCreator::P2SH(CScriptID(sourceScript));

        keyStore.AddCScript(sourceScript);
        VaultType vaultOwnershipType;
        IsMine(keyStore,someNonVaultScript,vaultOwnershipType);
        BOOST_CHECK_MESSAGE(vaultOwnershipType != NON_VAULT, "Did not detect non vault: p2sh - wrapping owned vault");
    }
    {
        CBasicKeyStore keyStore;
        keyStore.AddKey(keyChain.keys_[1]);
        CScript sourceScript = CScriptCreator::Vault(keyChain.pubkeys_[0].GetID(),keyChain.pubkeys_[1].GetID());
        CScript someNonVaultScript = CScriptCreator::P2SH(CScriptID(sourceScript));

        keyStore.AddCScript(sourceScript);
        VaultType vaultOwnershipType;
        IsMine(keyStore,someNonVaultScript,vaultOwnershipType);
        BOOST_CHECK_MESSAGE(vaultOwnershipType != NON_VAULT, "Did not detect non vault: p2sh - wrapping managed vault");
    }
}


BOOST_AUTO_TEST_SUITE_END()