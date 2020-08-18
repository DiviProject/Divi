// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "addrman.h"
#include "masternode.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "net.h"
#include "protocol.h"
#include "spork.h"

CActiveMasternode activeMasternode(masternodeConfig, fMasterNode);
extern CWallet* pwalletMain;

//
// Bootup the Masternode, look for a 10000 PIVX input and register on the network
//
void CActiveMasternode::ManageStatus()
{
    std::string errorMessage;

    if (!fMasterNode_) return;

    if (fDebug) LogPrintf("CActiveMasternode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if (Params().NetworkID() != CBaseChainParams::REGTEST && !masternodeSync.IsBlockchainSynced()) {
        status = ACTIVE_MASTERNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveMasternode::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if (status == ACTIVE_MASTERNODE_SYNC_IN_PROCESS)
        status = ACTIVE_MASTERNODE_INITIAL;

    if (status == ACTIVE_MASTERNODE_INITIAL) {
        CMasternode* pmn;
        pmn = mnodeman.Find(pubKeyMasternode);
        if (pmn != NULL) {
            pmn->Check();
            if (pmn->IsEnabled() && pmn->protocolVersion == PROTOCOL_VERSION)
                EnableHotColdMasterNode(pmn->vin, pmn->addr);
        }
    }

    if (status != ACTIVE_MASTERNODE_STARTED) {
        // Set defaults
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = "";

        if (pwalletMain->IsLocked()) {
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (pwalletMain->GetBalance() == 0) {
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (strMasterNodeAddr.empty()) {
            if (!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the masternodeaddr configuration option.";
                LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strMasterNodeAddr);
        }

        if (service.GetPort() != Params().GetDefaultPort())
        {
            notCapableReason = strprintf("Invalid port: %u - only 51472 is supported on mainnet.", service.GetPort());
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        LogPrintf("CActiveMasternode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        CNode* pnode = ConnectNode((CAddress)service, NULL, false);
        if (!pnode) {
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }
        pnode->Release();

        // Choose coins to use
        notCapableReason = "Could not find suitable coins!";
        LogPrintf("CActiveMasternode::ManageStatus() - %s\n", notCapableReason);
        return;
    }

    //send to all peers
    if (!SendMasternodePing(errorMessage)) {
        LogPrintf("CActiveMasternode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActiveMasternode::GetStatus()
{
    switch (status) {
    case ACTIVE_MASTERNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_MASTERNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Masternode";
    case ACTIVE_MASTERNODE_INPUT_TOO_NEW:
        return strprintf("Masternode input must have at least %d confirmations", MASTERNODE_MIN_CONFIRMATIONS);
    case ACTIVE_MASTERNODE_NOT_CAPABLE:
        return "Not capable masternode: " + notCapableReason;
    case ACTIVE_MASTERNODE_STARTED:
        return "Masternode successfully started";
    default:
        return "unknown";
    }
}

bool CActiveMasternode::SendMasternodePing(std::string& errorMessage)
{
    if (status != ACTIVE_MASTERNODE_STARTED) {
        errorMessage = "Masternode is not in a running status";
        return false;
    }

    CPubKey pubKeyMasternode;
    CKey keyMasternode;

    if (!CObfuScationSigner::SetKey(strMasterNodePrivKey, errorMessage, keyMasternode, pubKeyMasternode)) {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    LogPrintf("CActiveMasternode::SendMasternodePing() - Relay Masternode Ping vin = %s\n", vin.ToString());

    CMasternodePing mnp(vin);
    if (!mnp.Sign(keyMasternode, pubKeyMasternode)) {
        errorMessage = "Couldn't sign Masternode Ping";
        return false;
    }

    // Update lastPing for our masternode in Masternode list
    CMasternode* pmn = mnodeman.Find(vin);
    if (pmn != NULL) {
        if (pmn->IsPingedWithin(MASTERNODE_PING_SECONDS, mnp.sigTime)) {
            errorMessage = "Too early to send Masternode Ping";
            return false;
        }

        pmn->lastPing = mnp;
        mnodeman.mapSeenMasternodePing.insert(std::make_pair(mnp.GetHash(), mnp));

        //mnodeman.mapSeenMasternodeBroadcast.lastPing is probably outdated, so we'll update it
        CMasternodeBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if (mnodeman.mapSeenMasternodeBroadcast.count(hash)) mnodeman.mapSeenMasternodeBroadcast[hash].lastPing = mnp;

        mnp.Relay();

        return true;
    } else {
        // Seems like we are trying to send a ping while the Masternode is not registered in the network
        errorMessage = "Obfuscation Masternode List doesn't include our Masternode, shutting down Masternode pinging service! " + vin.ToString();
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

bool CActiveMasternode::Register(
    const CMasternodeConfig::CMasternodeEntry& configEntry,
    std::string& errorMessage,
    CMasternodeBroadcast& mnb,
    bool deferRelay)
{
    if(!CMasternodeBroadcastFactory::Create(
            configEntry,
            errorMessage,
            mnb,
            false,
            deferRelay))
    {
        return false;
    }

    addrman.Add(CAddress(mnb.addr), CNetAddr("127.0.0.1"), 2 * 60 * 60);
    return Register(mnb,deferRelay);
}

bool CActiveMasternode::Register(CMasternodeBroadcast &mnb, bool deferRelay)
{
    auto mnp = mnb.lastPing;
    mnodeman.mapSeenMasternodePing.insert(std::make_pair(mnp.GetHash(), mnp));

    LogPrintf("CActiveMasternode::Register() - Adding to Masternode list\n    service: %s\n    vin: %s\n", mnb.addr.ToString(), mnb.vin.ToString());
    mnodeman.mapSeenMasternodeBroadcast.insert(std::make_pair(mnb.GetHash(), mnb));
    masternodeSync.AddedMasternodeList(mnb.GetHash());

    CMasternode* pmn = mnodeman.Find(mnb.vin);
    if (pmn == NULL) {
        CMasternode mn(mnb);
        mnodeman.Add(mn);
    } else {
        pmn->UpdateFromNewBroadcast(mnb);
    }

    //send to all peers
    if(!deferRelay)
    {
        LogPrintf("CActiveMasternode::Register() - Relaying broadcast vin = %s\n", mnb.vin.ToString());
        mnb.Relay();
    }
    else
    {
        LogPrintf("CActiveMasternode::Register() - Deferring Relay vin = %s\n", mnb.vin.ToString());
    }


    return true;
}

// when starting a Masternode, this can enable to run as a hot wallet with no funds
bool CActiveMasternode::EnableHotColdMasterNode(CTxIn& newVin, CService& newService)
{
    if (!fMasterNode_)
    {
        return false;
    }

    if(status == ACTIVE_MASTERNODE_STARTED)
    {
        LogPrintf("CActiveMasternode::EnableHotColdMasterNode() - Cannot modify masternode that is already started.\n");
        return false;
    }

    bool transactionBelongsToMasternode = false;
    BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfigurations_.getEntries()) {
        if(
            std::strcmp(newVin.prevout.hash.ToString().c_str(), mne.getTxHash().c_str()) == 0 &&
            std::strcmp(std::to_string(newVin.prevout.n).c_str(), mne.getOutputIndex().c_str()) == 0
        )
        {
            transactionBelongsToMasternode = true;
            break;
        }
    }

    if(!transactionBelongsToMasternode)
    {
        return false;
    }

    //The values below are needed for signing mnping messages going forward
    status = ACTIVE_MASTERNODE_STARTED;
    vin = newVin;
    service = newService;

    LogPrintf("CActiveMasternode::EnableHotColdMasterNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}