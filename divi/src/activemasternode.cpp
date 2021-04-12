// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <activemasternode.h>
#include <masternode.h>
#include <masternodeman.h>
#include <net.h>
#include <obfuscation.h>
#include <protocol.h>
#include <masternodeconfig.h>
#include <MasternodePayeeData.h>
#include <MasternodeHelpers.h>
#include <Logging.h>

CActiveMasternode::CActiveMasternode(
    const CMasternodeConfig& masternodeConfigurations,
    const bool& masterNodeEnabled
    ): cs()
    , masternodeConfigurations_(masternodeConfigurations)
    , fMasterNode_(masterNodeEnabled)
    , addressHasBeenSet_(false)
    , masternodeKey_()
    , pubKeyMasternode()
    , vin()
    , service()
    , status(ACTIVE_MASTERNODE_INITIAL)
    , notCapableReason("")
{
}

bool CActiveMasternode::SetMasternodeAddress(const std::string& masternodeAddress)
{
    if(!masternodeAddress.empty())
    {
        service = CService(masternodeAddress);
        addressHasBeenSet_ = true;
        return service.IsValid();
    }
    return true;
}
bool CActiveMasternode::SetMasternodeKey(const std::string& privKeyString)
{
    std::string errorMessage = "";
    if (!CObfuScationSigner::SetKey(privKeyString, errorMessage, masternodeKey_, pubKeyMasternode)) {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }
    return true;
}

void CActiveMasternode::ManageStatus(CMasternodeMan& masternodeManager)
{
    std::string errorMessage ="";

    if (!fMasterNode_) return;

    LogPrint("masternode","CActiveMasternode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if (!IsBlockchainSynced()) {
        status = ACTIVE_MASTERNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveMasternode::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if (status == ACTIVE_MASTERNODE_SYNC_IN_PROCESS)
        status = ACTIVE_MASTERNODE_INITIAL;

    if (status == ACTIVE_MASTERNODE_INITIAL) {
        CMasternode* pmn;
        pmn = masternodeManager.Find(pubKeyMasternode);
        if (pmn != NULL) {
            masternodeManager.Check(*pmn);
            if (pmn->IsEnabled() && pmn->protocolVersion == PROTOCOL_VERSION)
                EnableHotColdMasterNode(pmn->vin, pmn->addr);
        }
    }

    if (status != ACTIVE_MASTERNODE_STARTED) {
        // Set defaults
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = "";

        LogPrintf("CActiveMasternode::ManageStatus() - failed to activate masternode. Check the local wallet\n");


        if (!addressHasBeenSet_ && !GetLocal(service))
        {
            notCapableReason = "Can't detect external address. Please use the masternodeaddr configuration option.";
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        const CAddress masternodeIPAddress = (CAddress)service;
        if(IsLocal(masternodeIPAddress))
        {
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        LogPrintf("CActiveMasternode::ManageStatus() - Checking inbound connection to '%s'\n", service);
        CNode* pnode = ConnectNode(masternodeIPAddress, NULL, false);
        if (!pnode)
        {
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
    if (!SendMasternodePing(masternodeManager,errorMessage)) {
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

bool CActiveMasternode::SendMasternodePing(CMasternodeMan& masternodeManager, std::string& errorMessage)
{
    if (status != ACTIVE_MASTERNODE_STARTED) {
        errorMessage = "Masternode is not in a running status";
        return false;
    }

    LogPrintf("CActiveMasternode::SendMasternodePing() - Relay Masternode Ping vin = %s\n", vin);

    CMasternodePing mnp = createCurrentPing(vin);
    if(!CObfuScationSigner::SignAndVerify<CMasternodePing>(mnp,masternodeKey_,pubKeyMasternode,errorMessage))
    {
        LogPrint("masternode","%s - %s",__func__,errorMessage);
        return false;
    }

    // Update lastPing for our masternode in Masternode list
    CMasternode* pmn = masternodeManager.Find(vin);
    if (pmn != NULL) {
        if (masternodeManager.IsTooEarlyToSendPingUpdate(*pmn,mnp.sigTime))
        {
            errorMessage = "Too early to send Masternode Ping";
            return false;
        }

        pmn->lastPing = mnp;
        masternodeManager.RecordSeenPing(mnp);

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

// when starting a Masternode, this can enable to run as a hot wallet with no funds
bool CActiveMasternode::IsThisMasternodeCollateral(const CTxIn& newVin) const
{
    BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfigurations_.getEntries()) {
        if(
            std::strcmp(newVin.prevout.hash.ToString().c_str(), mne.getTxHash().c_str()) == 0 &&
            std::strcmp(std::to_string(newVin.prevout.n).c_str(), mne.getOutputIndex().c_str()) == 0
        )
        {
            return true;
        }
    }
    return false;
}
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

    bool transactionBelongsToMasternode = IsThisMasternodeCollateral(newVin);

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
bool CActiveMasternode::SignMasternodeWinner(CMasternodePaymentWinner& winner) const
{
    std::string errorMessage = "";

    if(!CObfuScationSigner::SignAndVerify<CMasternodePaymentWinner>(winner,masternodeKey_,pubKeyMasternode,errorMessage))
    {
        LogPrint("masternode", "%s - Error: %s\n", __func__, errorMessage);
    }
    return true;
}

bool CActiveMasternode::IsOurBroadcast(const CMasternodeBroadcast& mnb, bool checkConfig) const
{
    return fMasterNode_ &&
        (checkConfig && vin== CTxIn())? IsThisMasternodeCollateral(mnb.vin) : mnb.vin.prevout == vin.prevout &&
        mnb.pubKeyMasternode == pubKeyMasternode;
}

bool CActiveMasternode::UpdatePing(CMasternodePing& mnp) const
{
    CMasternodePing updatedPing = createCurrentPing(mnp.vin);
    std::string errorMessage = "";
    if(CObfuScationSigner::SignAndVerify<CMasternodePing>(updatedPing,masternodeKey_,pubKeyMasternode,errorMessage))
    {
        CMasternodePing().swap(mnp,updatedPing);
        return true;
    }
    return false;
}
