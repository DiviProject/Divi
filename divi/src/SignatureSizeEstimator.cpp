#include <SignatureSizeEstimator.h>
#include <keystore.h>
#include <script/script.h>
#include <wallet_ismine.h>
#include <script/standard.h>
#include <destination.h>
#include <pubkey.h>

const unsigned SignatureSizeEstimator::MaximumScriptSigBytesForP2PK = 74u;//71-73u for sig, +1u for push
const unsigned SignatureSizeEstimator::MaximumScriptSigBytesForP2PKH = 140u;
unsigned SignatureSizeEstimator::MaxBytesNeededForSigning(const CKeyStore& keystore,const CScript& scriptPubKey,bool isSubscript)
{
    txnouttype whichType;
    std::vector<valtype> vSolutions;
    ExtractScriptPubKeyFormat(scriptPubKey,whichType,vSolutions);
    if(whichType == TX_PUBKEY)
    {
        return MaximumScriptSigBytesForP2PK;
    }
    else if(whichType == TX_PUBKEYHASH)
    {
        CKeyID keyID = CKeyID(uint160(vSolutions[0]));
        CPubKey pubkey;
        if(!keystore.GetPubKey(keyID,pubkey))
        {
            return MaximumScriptSigBytesForP2PKH;
        }
        return MaximumScriptSigBytesForP2PK+1u+pubkey.size();
    }
    else if(whichType == TX_MULTISIG)
    {
        unsigned numberOfKnownKeys = 0u;
        for(const auto& pubkeyData: vSolutions)
        {
            CKeyID keyID = CPubKey(pubkeyData).GetID();
            numberOfKnownKeys += keystore.HaveKey(keyID)? 1u:0u;
        }
        return 1u + numberOfKnownKeys*MaximumScriptSigBytesForP2PK;
    }
    else if(!isSubscript && whichType == TX_SCRIPTHASH)
    {
        CScript subscript;
        if(!keystore.GetCScript(CScriptID(uint160(vSolutions[0])), subscript))
        {
            return std::numeric_limits<unsigned>::max();
        }
        CScript serializedScript = CScript() << ToByteVector(subscript);
        return MaxBytesNeededForSigning(keystore,subscript,true)+serializedScript.size();
    }
    return std::numeric_limits<unsigned>::max();
}