// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "obfuscation.h"
#include "coincontrol.h"
#include "init.h"
#include "main.h"
#include "masternodeman.h"
#include "script/sign.h"
#include "swifttx.h"
#include "ui_interface.h"
#include "util.h"
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <boost/assign/list_of.hpp>
#include <openssl/rand.h>

bool CObfuScationSigner::IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey, CMasternode::Tier nMasternodeTier)
{
    CScript payee2;
    payee2 = GetScriptForDestination(pubkey.GetID());

    CTransaction txVin;
    uint256 hash;
    auto nCollateral = CMasternode::GetTierCollateralAmount(nMasternodeTier);
    if (GetTransaction(vin.prevout.hash, txVin, hash, true)) {
        BOOST_FOREACH (CTxOut out, txVin.vout) {
            if (out.nValue == nCollateral) {
                if (out.scriptPubKey == payee2) return true;
            }
        }
    }

    return false;
}

bool CObfuScationSigner::SetKey(std::string strSecret, std::string& errorMessage, CKey& key, CPubKey& pubkey)
{
    CBitcoinSecret vchSecret;
    bool fGood = vchSecret.SetString(strSecret);

    if (!fGood) {
        errorMessage = _("Invalid private key.");
        return false;
    }

    key = vchSecret.GetKey();
    pubkey = key.GetPubKey();

    return true;
}

bool CObfuScationSigner::GetKeysFromSecret(std::string strSecret, CKey& keyRet, CPubKey& pubkeyRet)
{
    CBitcoinSecret vchSecret;

    if (!vchSecret.SetString(strSecret)) return false;

    keyRet = vchSecret.GetKey();
    pubkeyRet = keyRet.GetPubKey();

    return true;
}

bool CObfuScationSigner::SignMessage(std::string strMessage, std::string& errorMessage, vector<unsigned char>& vchSig, CKey key)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    if (!key.SignCompact(ss.GetHash(), vchSig)) {
        errorMessage = _("Signing failed.");
        return false;
    }

    return true;
}

bool CObfuScationSigner::VerifyMessage(CPubKey pubkey, const vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey2;
    if (!pubkey2.RecoverCompact(ss.GetHash(), vchSig)) {
        errorMessage = _("Error recovering public key.");
        return false;
    }

    if (fDebug && pubkey2.GetID() != pubkey.GetID())
        LogPrintf("CObfuScationSigner::VerifyMessage -- keys don't match: %s %s\n", pubkey2.GetID().ToString(), pubkey.GetID().ToString());

    return (pubkey2.GetID() == pubkey.GetID());
}

//TODO: Rename/move to core
void ThreadCheckObfuScationPool()
{
    if (fLiteMode) return; //disable all Obfuscation/Masternode related functionality

    // Make this thread recognisable as the wallet flushing thread
    RenameThread("divi-obfuscation");

    unsigned int c = 0;

    while (true) {
        MilliSleep(1000);
        //LogPrintf("ThreadCheckObfuScationPool::check timeout\n");

        // try to sync from all available nodes, one step at a time
        masternodeSync.Process();

        if (masternodeSync.IsBlockchainSynced()) {
            c++;

            // check if we should activate or ping every few minutes,
            // start right after sync is considered to be done
            if (c % MASTERNODE_PING_SECONDS == 1) activeMasternode.ManageStatus();

            if (c % 60 == 0) {
                mnodeman.CheckAndRemoveInnactive();
                mnodeman.ProcessMasternodeConnections();
                masternodePayments.CheckAndRemove();
                CleanTransactionLocksList();
            }

            //if(c % MASTERNODES_DUMP_SECONDS == 0) DumpMasternodes();

//            obfuScationPool.CheckTimeout();
//            obfuScationPool.CheckForCompleteQueue();

//            if (obfuScationPool.GetState() == POOL_STATUS_IDLE && c % 15 == 0) {
//                obfuScationPool.DoAutomaticDenominating();
//            }
        }
    }
}
