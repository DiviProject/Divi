#include <SignatureSizeEstimator.h>
#include <keystore.h>
#include <script/script.h>
#include <wallet_ismine.h>
#include <script/standard.h>
#include <destination.h>
#include <pubkey.h>

const unsigned SignatureSizeEstimator::MaximumScriptSigBytesForP2PK = 73u;//72u for sig, +1u for push
const unsigned SignatureSizeEstimator::MaximumScriptSigBytesForP2PKH = 139u;
unsigned SignatureSizeEstimator::MaxBytesNeededForSigning(const CKeyStore& keystore,const CScript& scriptPubKey)
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
        return 72u+2u+pubkey.size();
    }
    return std::numeric_limits<unsigned>::max();
}