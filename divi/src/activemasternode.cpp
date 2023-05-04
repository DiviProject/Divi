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
    ): masternodeConfigurations_(masternodeConfigurations)
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

bool CActiveMasternode::VerifyStartedStatus()
{
    if (status != ACTIVE_MASTERNODE_STARTED) {
        // Set defaults
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = "";

        LogPrintf("%s - failed to activate masternode. Check the local wallet\n",__func__);


        if (!addressHasBeenSet_ && !GetLocal(service))
        {
            notCapableReason = "Can't detect external address. Please use the masternodeaddr configuration option.";
            LogPrintf("%s - not capable: %s\n",__func__, notCapableReason);
            return false;
        }

        const CAddress masternodeIPAddress = (CAddress)service;
        if(IsLocal(masternodeIPAddress))
        {
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("%s - not capable: %s\n",__func__, notCapableReason);
            return false;
        }

        LogPrintf("%s - Checking inbound connection to '%s'\n",__func__, service);
        if (!CheckNodeIsAcceptingConnections(masternodeIPAddress))
        {
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("%s - not capable: %s\n",__func__, notCapableReason);
            return false;
        }

        // Choose coins to use
        notCapableReason = "Could not find suitable coins!";
        LogPrintf("%s - %s\n",__func__, notCapableReason);
        return false;
    }
    return true;
}

bool CActiveMasternode::TryUpdatingPing(CMasternode* pmn)
{
    //send to all peers
    LogPrintf("%s - trying to update ping\n",__func__);
    std::string errorMessage ="";
    if (!UpdateLocalMasternodePing(pmn,errorMessage)) {
        LogPrintf("%s - Error on Ping: %s\n",__func__, errorMessage);
        return false;
    }
    return true;
}

bool CActiveMasternode::IsPendingActivation() const
{
    return status == ACTIVE_MASTERNODE_INITIAL ||
        status == ACTIVE_MASTERNODE_SYNC_IN_PROCESS ||
        status == ACTIVE_MASTERNODE_NOT_CAPABLE;
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

bool CActiveMasternode::UpdateLocalMasternodePing(CMasternode* pmn, std::string& errorMessage)
{
    if (status != ACTIVE_MASTERNODE_STARTED) {
        errorMessage = "Masternode is not in a running status";
        return false;
    }

    LogPrintf("CActiveMasternode::UpdateLocalMasternodePing() - Relay Masternode Ping vin = %s\n", vin);

    CMasternodePing mnp = createCurrentPing(vin);
    if(!CObfuScationSigner::SignAndVerify<CMasternodePing>(mnp,masternodeKey_,pubKeyMasternode,errorMessage))
    {
        LogPrint("masternode","%s - %s",__func__,errorMessage);
        return false;
    }

    // Update lastPing for our masternode in Masternode list
    if (pmn != NULL) {
        if (IsTooEarlyToSendPingUpdate(*pmn,mnp.sigTime))
        {
            errorMessage = "Too early to send Masternode Ping";
            return false;
        }

        pmn->lastPing = mnp;
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
bool CActiveMasternode::EnablePinging(CTxIn& newVin, CService& newService)
{
    if (!fMasterNode_)
    {
        return false;
    }
    LogPrintf("%s - trying to enable pinging\n",__func__);

    if(status == ACTIVE_MASTERNODE_STARTED)
    {
        LogPrintf("CActiveMasternode::EnablePinging() - Cannot modify masternode that is already started.\n");
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

    LogPrintf("CActiveMasternode::EnablePinging() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
bool CActiveMasternode::SignMasternodeWinner(CMasternodePaymentWinner& winner) const
{
    std::string errorMessage = "";
    LogPrint("masternode", "%s - signing mn winner\n",__func__);
    if(!CObfuScationSigner::SignAndVerify<CMasternodePaymentWinner>(winner,masternodeKey_,pubKeyMasternode,errorMessage))
    {
        LogPrint("masternode", "%s - Error: %s\n", __func__, errorMessage);
    }
    return true;
}

bool CActiveMasternode::IsOurBroadcast(const CMasternodeBroadcast& mnb, bool checkConfig) const
{
    LogPrint("masternode", "%s - mnb.vin: %s vs. local vin: %s\n", __func__, mnb.vin, vin);
    const bool matchesOurKeyAndUTXO =  fMasterNode_ &&
        (checkConfig && vin== CTxIn())? IsThisMasternodeCollateral(mnb.vin) : mnb.vin.prevout == vin.prevout &&
        mnb.pubKeyMasternode == pubKeyMasternode;
    LogPrint("masternode", "%s - mnb.vin: %s vs. local vin: %s is_ours: %u\n",__func__, mnb.vin, vin, static_cast<unsigned>(matchesOurKeyAndUTXO));
    return matchesOurKeyAndUTXO;
}

bool CActiveMasternode::UpdatePing(CMasternodePing& mnp) const
{
    LogPrint("masternode", "%s - updating ping\n",__func__);
    CMasternodePing updatedPing = createCurrentPing(mnp.vin);
    std::string errorMessage = "";
    if(CObfuScationSigner::SignAndVerify<CMasternodePing>(updatedPing,masternodeKey_,pubKeyMasternode,errorMessage))
    {
        CMasternodePing().swap(mnp,updatedPing);
        return true;
    }
    return false;
}

void CActiveMasternode::FlagBlockchainSyncRequired()
{
    if(!fMasterNode_) return;
    status = ACTIVE_MASTERNODE_SYNC_IN_PROCESS;
    LogPrintf("%s - %s\n",__func__, GetStatus());
}