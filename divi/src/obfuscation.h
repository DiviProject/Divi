// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OBFUSCATION_H
#define OBFUSCATION_H

#include "masternode-payments.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "obfuscation-relay.h"
#include "sync.h"

class CTxIn;
class CMasterNodeVote;
class CBitcoinAddress;
// class CActiveMasternode;

// pool states for mixing
#define POOL_STATUS_UNKNOWN 0              // waiting for update
#define POOL_STATUS_IDLE 1                 // waiting for update
#define POOL_STATUS_QUEUE 2                // waiting in a queue
#define POOL_STATUS_ACCEPTING_ENTRIES 3    // accepting entries
#define POOL_STATUS_FINALIZE_TRANSACTION 4 // master node will broadcast what it accepted
#define POOL_STATUS_SIGNING 5              // check inputs/outputs, sign final tx
#define POOL_STATUS_TRANSMISSION 6         // transmit transaction
#define POOL_STATUS_ERROR 7                // error
#define POOL_STATUS_SUCCESS 8              // success

// status update message constants
#define MASTERNODE_ACCEPTED 1
#define MASTERNODE_REJECTED 0
#define MASTERNODE_RESET -1


/** Helper object for signing and checking signatures
 */
struct CObfuScationSigner
{
    /// Is the inputs associated with this public key? (and there is 10000 PIV - checking if valid masternode)
    static bool IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey, MasternodeTier nMasternodeTier);
    /// Set the private/public key values, returns true if successful
    static bool GetKeysFromSecret(std::string strSecret, CKey& keyRet, CPubKey& pubkeyRet);
    /// Set the private/public key values, returns true if successful
    static bool SetKey(std::string strSecret, std::string& errorMessage, CKey& key, CPubKey& pubkey);
    /// Sign the message, returns true if successful
    static bool SignMessage(std::string strMessage, std::string& errorMessage, std::vector<unsigned char>& vchSig, CKey key);
    /// Verify the message, returns true if succcessful
    static bool VerifyMessage(CPubKey pubkey, const std::vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage);
};

void ThreadCheckObfuScationPool();

#endif
