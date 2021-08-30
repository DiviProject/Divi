// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEMASTERNODE_H
#define ACTIVEMASTERNODE_H

#include <key.h>
#include <pubkey.h>
#include <netbase.h>
#include <primitives/transaction.h>

#define ACTIVE_MASTERNODE_INITIAL 0 // initial state
#define ACTIVE_MASTERNODE_SYNC_IN_PROCESS 1
#define ACTIVE_MASTERNODE_INPUT_TOO_NEW 2
#define ACTIVE_MASTERNODE_NOT_CAPABLE 3
#define ACTIVE_MASTERNODE_STARTED 4

class CMasternodeBroadcast;
class CMasternodePing;
class CMasternodeConfig;
class CMasternodePaymentWinner;
class CMasternode;

// Responsible for activating the Masternode and pinging the network
class CActiveMasternode
{
private:
    bool UpdateLocalMasternodePing(CMasternode* pmn,std::string& errorMessage);
    bool IsThisMasternodeCollateral(const CTxIn& newVin) const;
private:
    // critical section to protect the inner data structures
    const CMasternodeConfig& masternodeConfigurations_;
    const bool& fMasterNode_;
    bool addressHasBeenSet_;
    CKey masternodeKey_;
public:
    // Initialized by init.cpp
    // Keys for the main Masternode
    CPubKey pubKeyMasternode;

    // Initialized while registering Masternode
    CTxIn vin;
    CService service;

    int status;
    std::string notCapableReason;

    CActiveMasternode(const CMasternodeConfig& masternodeConfigurations,const bool& masterNodeEnabled);

    /// Manage status of main Masternode
    bool SetMasternodeKey(const std::string& privKeyString);
    bool SetMasternodeAddress(const std::string& masternodeAddress);
    bool TryUpdatingPing(CMasternode* pmn);
    std::string GetStatus();
    void FlagBlockchainSyncRequired();
    bool VerifyStartedStatus();
    bool IsPendingActivation() const;

    /// Enable cold wallet mode (run a Masternode with no funds)
    bool EnablePinging(CTxIn& vin, CService& addr);
    bool SignMasternodeWinner(CMasternodePaymentWinner& winner) const;
    bool IsOurBroadcast(const CMasternodeBroadcast& mnb, bool checkConfig = false) const;
    bool UpdatePing(CMasternodePing& mnp) const;
};
#endif
