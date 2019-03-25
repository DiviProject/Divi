// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <messagesigner.h>
#include <key_io.h>
#include <hash.h>
#include <validation.h> // For strMessageMagic
#include <tinyformat.h>
#include <key_io.h>
#include <util/strencodings.h>

bool CMessageSigner::IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey, CMasternode::Tier nMasternodeTier)
{
    CScript payee2;
    payee2 = GetScriptForDestination(pubkey.GetID());

    CTransactionRef txVin;
    uint256 hash;
    auto nCollateral = CMasternode::GetTierCollateralAmount(nMasternodeTier);
    if (GetTransaction(vin.prevout.hash, txVin, Params().GetConsensus(), hash, true)) {
        for (CTxOut out : txVin->vout) {
            if (out.nValue == nCollateral) {
                if (out.scriptPubKey == payee2) return true;
            }
        }
    }

    return false;
}

bool CMessageSigner::GetKeysFromSecret(const std::string strSecret, CKey& keyRet, CPubKey& pubkeyRet)
{   
    CKey decodedKey = DecodeSecret(strSecret);

    if(!decodedKey.IsValid())
        return false;

    keyRet = decodedKey;
    pubkeyRet = decodedKey.GetPubKey();

    return true;
}

bool CMessageSigner::SignMessage(const std::string strMessage, std::vector<unsigned char>& vchSigRet, const CKey &key, CPubKey::InputScriptType scriptType)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    return CHashSigner::SignHash(ss.GetHash(), key, scriptType, vchSigRet);
}

bool CMessageSigner::VerifyMessage(const CTxDestination &address, const std::vector<unsigned char>& vchSig, const std::string strMessage, std::string& strErrorRet)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    return CHashSigner::VerifyHash(ss.GetHash(), address, vchSig, strErrorRet);
}

bool CHashSigner::SignHash(const uint256& hash, const CKey &key, CPubKey::InputScriptType scriptType, std::vector<unsigned char>& vchSigRet)
{
    return key.SignCompact(hash, vchSigRet, scriptType);
}

bool CHashSigner::VerifyHash(const uint256& hash, const CTxDestination &address, const std::vector<unsigned char>& vchSig, std::string& strErrorRet)
{
    CPubKey pubkeyFromSig;
    CPubKey::InputScriptType inputScriptType;
    if(!pubkeyFromSig.RecoverCompact(hash, vchSig, inputScriptType)) {
        strErrorRet = "Error recovering public key.";
        return false;
    }


    auto GetDestForKey = [](const CKeyID &keyID, CPubKey::InputScriptType type) -> CTxDestination {
        switch(type) {
        case CPubKey::InputScriptType::SPENDP2PKH: return keyID;
        case CPubKey::InputScriptType::SPENDP2SHWITNESS: return CScriptID(GetScriptForDestination(WitnessV0KeyHash(keyID)));
        case CPubKey::InputScriptType::SPENDWITNESS: return WitnessV0KeyHash(keyID);
        default:
            break;
        }

        return CNoDestination();
    };

    CTxDestination recoveredAddress = GetDestForKey(pubkeyFromSig.GetID(), inputScriptType);

    if(boost::get<CNoDestination>(&recoveredAddress))
    {
        strErrorRet = "Invalid inputScriptType";
        return false;
    }

    // not all classes in CTxDestination support operator!= on some compilers
    if(!(address == recoveredAddress)) {
        strErrorRet = strprintf("Addresses don't match: address=%s, addressFromSig=%s, hash=%s, vchSig=%s",
                    EncodeDestination(address), EncodeDestination(recoveredAddress), hash.ToString(),
                    EncodeBase64(&vchSig[0], vchSig.size()));
        return false;
    }

    return true;
}

bool CMessageSigner::SetKey(std::string strSecret, CKey &key, CPubKey &pubkey)
{
    CKey keyTmp = DecodeSecret(strSecret);

    if (!keyTmp.IsValid()) {
        return false;
    }

    key = keyTmp;
    pubkey = key.GetPubKey();

    return true;
}
