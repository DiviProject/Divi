#include <blocksigner.h>
#include <keystore.h>
#include <primitives/block.h>
#include <messagesigner.h>
#include <util/system.h>
#include <key_io.h>

static CPubKey::InputScriptType GetScriptTypeFromDestination(const CTxDestination &dest)
{
    if (boost::get<CKeyID>(&dest)) {
        return CPubKey::InputScriptType::SPENDP2PKH;
    }
    if (boost::get<WitnessV0KeyHash>(&dest)) {
        return CPubKey::InputScriptType::SPENDWITNESS;
    }
    if (boost::get<CScriptID>(&dest)) {
        return CPubKey::InputScriptType::SPENDP2SHWITNESS;
    }
    return CPubKey::InputScriptType::SPENDUNKNOWN;
}

CBlockSigner::CBlockSigner(CBlock &block, const CKeyStore *keystore) :
    refBlock(block),
    refKeystore(keystore)
{

}

#include <util/strencodings.h>

bool CBlockSigner::SignBlock()
{
    CKey keySecret;
    CPubKey::InputScriptType scriptType;

    CTxDestination destination;
    if(refBlock.IsProofOfStake())
    {
        const CTxOut& txout = refBlock.vtx[1]->vout[1];

        if(!ExtractDestination(txout.scriptPubKey, destination))
        {
            return error("Failed to extract destination while signing: %s\n", txout.ToString());
        }

        LogPrintf("signing with address: %s\n", EncodeDestination(destination));

        auto keyid = GetKeyForDestination(*refKeystore, destination);
        {
            if (keyid.IsNull()) {
                return error("CBlockSigner::SignBlock() : failed to get key for destination, won't sign.");
            }
            if (!refKeystore->GetKey(keyid, keySecret)) {
                return error("CBlockSigner::SignBlock() : Private key for address %s not known", EncodeDestination(destination));
            }

            scriptType = GetScriptTypeFromDestination(destination);
        }

        LogPrintf("preparing to sign block: %s %s %d\n", refBlock.GetHash().ToString(), keyid.ToString(), static_cast<int>(scriptType));
    }
    //?
    bool result = CHashSigner::SignHash(refBlock.GetHash(), keySecret, scriptType, refBlock.vchBlockSig);
    std::string strError;
    LogPrintf("Signed, result: %d %d %s %s\n",
              result, CHashSigner::VerifyHash(refBlock.GetHash(), destination, refBlock.vchBlockSig, strError),
              EncodeBase64(&refBlock.vchBlockSig[0], refBlock.vchBlockSig.size()), strError);
    return result;
}

bool CBlockSigner::CheckBlockSignature() const
{
    if(refBlock.IsProofOfWork())
        return true;

    if(refBlock.vchBlockSig.empty())
        return false;

    const CTxOut& txout = refBlock.vtx[1]->vout[1];
    auto hashMessage = refBlock.GetHash();

    std::vector<std::vector<unsigned char>> vSolutions;
    txnouttype whichType = Solver(txout.scriptPubKey, vSolutions);

    LogPrintf("Checking block, whichType: %d\n", whichType);

    CTxDestination destination;
    if(whichType == TX_PUBKEY)
    {
        CPubKey pubkey(vSolutions[0]);
        bool isValid = pubkey.IsValid() && pubkey.Verify(hashMessage, refBlock.vchBlockSig);
        if(isValid)
        {
            return true;
        }

        destination = pubkey.GetID();
    }
    else if(!ExtractDestination(txout.scriptPubKey, destination))
    {
        return error("CBlockSigner::CheckBlockSignature() : failed to extract destination from script: %s", txout.scriptPubKey.ToString());

    }

    std::string strError;
    bool result = CHashSigner::VerifyHash(hashMessage, destination, refBlock.vchBlockSig, strError);
    if(!result)
    {
        LogPrintf("Failed to verify hash, %s\n", strError);
    }
    return result;
}
