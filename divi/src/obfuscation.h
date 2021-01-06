// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OBFUSCATION_H
#define OBFUSCATION_H

#include <key.h>
#include <pubkey.h>
/** Helper object for signing and checking signatures **/
struct CObfuScationSigner
{
    /// Set the private/public key values, returns true if successful
    static bool GetKeysFromSecret(std::string strSecret, CKey& keyRet, CPubKey& pubkeyRet);
    /// Set the private/public key values, returns true if successful
    static bool SetKey(std::string strSecret, std::string& errorMessage, CKey& key, CPubKey& pubkey);
    /// Sign the message, returns true if successful
    static bool SignMessage(std::string strMessage, std::string& errorMessage, std::vector<unsigned char>& vchSig, CKey key);
    /// Verify the message, returns true if succcessful
    static bool VerifyMessage(CPubKey pubkey, const std::vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage);
};

void ThreadMasternodeBackgroundSync();

#endif
