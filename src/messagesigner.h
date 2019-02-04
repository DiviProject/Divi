// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MESSAGESIGNER_H
#define MESSAGESIGNER_H

#include <key.h>
#include <script/standard.h>
#include <masternodes/masternode.h>

/** Helper class for signing messages and checking their signatures
 */
class CMessageSigner
{
public:
    /// Is the inputs associated with this public key? (and there is 10000 PIV - checking if valid masternode)
    static bool IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey, CMasternode::Tier nMasternodeTier);
    /// Set the private/public key values, returns true if successful
    static bool GetKeysFromSecret(const std::string strSecret, CKey& keyRet, CPubKey& pubkeyRet);
    /// Sign the message, returns true if successful
    static bool SignMessage(const std::string strMessage, std::vector<unsigned char>& vchSigRet, const CKey &key, CPubKey::InputScriptType scriptType);
    /// Verify the message signature, returns true if succcessful
    static bool VerifyMessage(const CTxDestination &address, const std::vector<unsigned char>& vchSig,
                              const std::string strMessage, std::string& strErrorRet);

    static bool SetKey(std::string strSecret, CKey& key, CPubKey& pubkey);

};

/** Helper class for signing hashes and checking their signatures
 */
class CHashSigner
{
public:
    /// Sign the hash, returns true if successful
    static bool SignHash(const uint256& hash, const CKey &key, CPubKey::InputScriptType scriptType, std::vector<unsigned char>& vchSigRet);
    /// Verify the hash signature, returns true if succcessful
    static bool VerifyHash(const uint256& hash, const CTxDestination &address, const std::vector<unsigned char>& vchSig, std::string& strErrorRet);
};

#endif
