#include <primitives/block.h>
#include <primitives/transaction.h>
#include <utilstrencodings.h>
#include <script/script.h>
#include <script/standard.h>
#include <pubkey.h>
#include <key.h>
#include <memory>
#include <script/StakingVaultScript.h>
#include <keystore.h>

#include "test_only.h"
#include <random.h>
#include <string>

CPubKey AddKeyPair(CKeyStore& keystore)
{
    CKey key;
    CPubKey pubkey;
    auto data = GetRandHash();
    key.Set(data.begin(),data.end(),true);
    pubkey = key.GetPubKey();
    keystore.AddKeyPubKey(key,pubkey);
    return pubkey;
}
CScript ConvertToP2SH(const CScript& redeemScript)
{
    valtype redeemScriptHash = ToByteVector(CScriptID(redeemScript));
    return CScript() << OP_HASH160 << redeemScriptHash << OP_EQUAL;
}

class BlockSignatureTestFixture
{
public:
    CBlock block;
    CMutableTransaction coinstake;
    CBasicKeyStore ownerKeyStore;// Use separate keystores for each "node"
    CBasicKeyStore vaultKeyStore;
    CPubKey ownerPKey;
    CPubKey vaultPKey;
    void MarkCoinstake()
    {
        coinstake.vin.clear();
        coinstake.vout.clear();
        CScript scriptEmpty;
        scriptEmpty.clear();
        coinstake.vout.push_back(CTxOut(0, scriptEmpty));
    }
public:


    BlockSignatureTestFixture(
        ): block()
        , coinstake()
        , ownerKeyStore()
        , vaultKeyStore()
        , ownerPKey()
        , vaultPKey()
    {
        MarkCoinstake();
        ownerPKey = AddKeyPair(ownerKeyStore);
        vaultPKey = AddKeyPair(vaultKeyStore);
    }

    CScript createDummyVaultScriptSig(const CScript& redeeemScript)
    {
        valtype sig;
        CKey vaultPrivKey;
        vaultKeyStore.GetKey(vaultPKey.GetID(),vaultPrivKey);
        uint256 dummyHash = GetRandHash();
        vaultPrivKey.Sign(dummyHash,sig);
        CScript scriptSig;
        scriptSig << sig << ToByteVector(redeeemScript);
        return scriptSig;
    }
    void addStakingP2SHCoinstake(const CScript& redeemScript)
    {
        CScript p2shScript = ConvertToP2SH(redeemScript);
        vaultKeyStore.AddCScript(redeemScript);

        coinstake.vin.push_back( CTxIn(uint256S("0x25"),0, createDummyVaultScriptSig(redeemScript)) );
        coinstake.vout.push_back( CTxOut(0,p2shScript) );

        block.vtx.emplace_back();
        block.vtx.push_back(coinstake);
    }
};


BOOST_FIXTURE_TEST_SUITE(BlockSignatureTests, BlockSignatureTestFixture)

BOOST_AUTO_TEST_CASE(willAllowP2SHStakingVaultCoinstakeInBlock)
{
    CScript redeemScript = CreateStakingVaultScript(
            ToByteVector(ownerPKey.GetID()),
            ToByteVector(vaultPKey.GetID()));

    addStakingP2SHCoinstake(redeemScript);
    // Preconditions
    BOOST_CHECK_MESSAGE(block.IsProofOfStake(),"Block isnt PoS!");
    // Test
    BOOST_CHECK_MESSAGE(block.SignBlock(vaultKeyStore),"Failed to sign block!");
    BOOST_CHECK_MESSAGE(block.CheckBlockSignature(),"Failed to verify signature!");
}

BOOST_AUTO_TEST_SUITE_END()