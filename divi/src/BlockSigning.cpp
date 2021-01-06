// Copyright (c) 2020 The DIVI Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "BlockSigning.h"

#include "keystore.h"
#include "primitives/block.h"
#include "script/standard.h"
#include "script/sign.h"
#include "Logging.h"

#include <vector>

bool
SignBlock (const CKeyStore& keystore, CBlock& block)
{
    std::vector<valtype> vSolutions;
    txnouttype whichType;

    if(!block.IsProofOfStake())
    {
        for(const auto& txout : block.vtx[0].vout)
        {
            if (!ExtractScriptPubKeyFormat(txout.scriptPubKey, whichType, vSolutions))
                continue;

            if (whichType == TX_PUBKEY)
            {
                // Sign
                CKeyID keyID;
                keyID = CKeyID(uint160(vSolutions[0]));

                CKey key;
                if (!keystore.GetKey(keyID, key))
                    return false;

                //vector<unsigned char> vchSig;
                if (!key.SignCompact(block.GetHash(), block.vchBlockSig))
                     return false;

                return true;
            }
        }
    }
    else
    {
        const CTxOut& txout = block.vtx[1].vout[1];

        if (!ExtractScriptPubKeyFormat(txout.scriptPubKey, whichType, vSolutions))
            return false;

        if (whichType == TX_PUBKEYHASH)
        {

            CKeyID keyID;
            keyID = CKeyID(uint160(vSolutions[0]));

            CKey key;
            if (!keystore.GetKey(keyID, key))
                return false;

            //vector<unsigned char> vchSig;
            if (!key.SignCompact(block.GetHash(), block.vchBlockSig))
                 return false;

            return true;

        }
        else if(whichType == TX_PUBKEY)
        {
            CKeyID keyID;
            keyID = CPubKey(vSolutions[0]).GetID();
            CKey key;
            if (!keystore.GetKey(keyID, key))
                return false;

            //vector<unsigned char> vchSig;
            if (!key.Sign(block.GetHash(), block.vchBlockSig))
                 return false;

            return true;
        }
        else if(whichType == TX_VAULT)
        {
            const CKeyID keyID = CKeyID(uint160(vSolutions[1]));

            CKey key;
            if (!keystore.GetKey(keyID, key))
                return false;

            return key.SignCompact(block.GetHash(), block.vchBlockSig);
        }
    }

    LogPrintf("%s - Sign failed\n", __func__);
    return false;
}

bool
CheckBlockSignature (const CBlock& block)
{
    if (block.IsProofOfWork())
        return block.vchBlockSig.empty();

    std::vector<valtype> vSolutions;
    txnouttype whichType;

    const CTxOut& txout = block.vtx[1].vout[1];

    if (!ExtractScriptPubKeyFormat(txout.scriptPubKey, whichType, vSolutions))
    {
        return false;
    }

    if (block.vchBlockSig.empty())
        return false;

    if (whichType == TX_PUBKEY)
    {
        const auto& vchPubKey = vSolutions[0];
        const CPubKey pubkey(vchPubKey);
        if (!pubkey.IsValid())
          return false;

        return pubkey.Verify(block.GetHash(), block.vchBlockSig);
    }
    else if(whichType == TX_PUBKEYHASH)
    {
        const auto& vchPubKey = vSolutions[0];
        const CKeyID keyID = CKeyID(uint160(vchPubKey));

        CPubKey pubkeyFromSig;
        if(!pubkeyFromSig.RecoverCompact(block.GetHash(), block.vchBlockSig)) {
            return false;
        }

        return keyID == pubkeyFromSig.GetID();
    }
    else if (whichType == TX_VAULT)
    {
        const auto& vchPubKey = vSolutions[1];
        const CKeyID keyID = CKeyID(uint160(vchPubKey));

        CPubKey pubkeyFromSig;
        if(!pubkeyFromSig.RecoverCompact(block.GetHash(), block.vchBlockSig)) {
            return false;
        }

        return keyID == pubkeyFromSig.GetID();
    }

    return false;
}
