#include <MasternodeModule.h>

#include <utiltime.h>

#include <chrono>
#include <util.h>

#include <masternode-sync.h>
#include <masternode-payments.h>
#include <masternodeman.h>
#include <activemasternode.h>
#include <chainparams.h>
#include <ui_interface.h>
#include <Logging.h>
#include <Settings.h>
#include <masternodeconfig.h>
#include <chain.h>
#include <version.h>
#include <streams.h>
#include <net.h>

extern bool fMasterNode;
CMasternodeSync masternodeSync;
CActiveMasternode activeMasternode(masternodeConfig, fMasterNode);

template <typename T>
static T readFromHex(std::string hexString)
{
    std::vector<unsigned char> hex = ParseHex(hexString);
    CDataStream ss(hex,SER_NETWORK,PROTOCOL_VERSION);
    T object;
    ss >> object;
    return object;
}


MasternodeStartResult StartMasternode(std::string alias, bool deferRelay)
{
    MasternodeStartResult result;
    for(const auto& configEntry : masternodeConfig.getEntries())
    {
        if(configEntry.getAlias() != alias)
            continue;

        CMasternodeBroadcast mnb;

        if(!CMasternodeBroadcastFactory::Create(
                configEntry,
                result.errorMessage,
                mnb,
                false,
                deferRelay))
        {
            result.status = false;
            return result;
        }

        if (deferRelay)
        {
            CDataStream ss(SER_NETWORK,PROTOCOL_VERSION);
            ss << mnb;

            result.status = true;
            result.broadcastData = HexStr(ss.str());
            return result;
        }

        if(!mnodeman.ProcessBroadcast(activeMasternode, masternodeSync,nullptr, mnb))
        {
            LogPrintf("%s - Relaying broadcast vin = %s\n",__func__, mnb.vin.ToString());
            result.status = false;
            result.errorMessage = "Error processing broadcast";
            return result;
        }

        result.status = true;
        return result;
    }
    result.status = false;
    result.errorMessage = "Invalid alias, couldn't find MN. Check your masternode.conf file";
    return result;
}

bool RelayMasternodeBroadcast(std::string hexData, std::string signature)
{
    CMasternodeBroadcast mnb = readFromHex<CMasternodeBroadcast>(hexData);
    if(!signature.empty())
    {
        mnb.signature = ParseHex(signature);
        if(activeMasternode.IsOurBroadcast(mnb,true))
        {
            if(activeMasternode.UpdatePing(mnb.lastPing))
            {
                LogPrint("masternode","Ping updated successfully!\n");
            }
            else
            {
                LogPrint("masternode","Ping not updated! Failure to sign!\n");
            }
        }
        else
        {
            LogPrint("masternode","This broadcast does not belong to us!\n");
        }
    }

    if (mnodeman.ProcessBroadcast(activeMasternode, masternodeSync,nullptr, mnb))
    {
        return true;
    }
    else
    {
        return false;
    }
}

const CMasternodeSync& GetMasternodeSync()
{
    return masternodeSync;
}

void ForceMasternodeResync()
{
    masternodeSync.Reset();
}

bool ShareMasternodePingWithPeer(CNode* peer,const uint256& inventoryHash)
{
    if (mnodeman.mapSeenMasternodePing.count(inventoryHash)) {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss.reserve(1000);
        ss << mnodeman.mapSeenMasternodePing[inventoryHash];
        peer->PushMessage("mnp", ss);
        return true;
    }
    return false;
}

bool ShareMasternodeBroadcastWithPeer(CNode* peer,const uint256& inventoryHash)
{
    if (mnodeman.mapSeenMasternodeBroadcast.count(inventoryHash))
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss.reserve(1000);
        ss << mnodeman.mapSeenMasternodeBroadcast[inventoryHash];
        peer->PushMessage("mnb", ss);
        return true;
    }
    return false;
}

bool MasternodeIsKnown(const uint256& inventoryHash)
{
    if (mnodeman.mapSeenMasternodeBroadcast.count(inventoryHash))
    {
        masternodeSync.AddedMasternodeList(inventoryHash);
        return true;
    }
    return false;
}

bool MasternodeWinnerIsKnown(const uint256& inventoryHash)
{
    if (masternodePayments.GetPaymentWinnerForHash(inventoryHash) != nullptr)
    {
        masternodeSync.AddedMasternodeWinner(inventoryHash);
        return true;
    }
    return false;
}

void ProcessMasternodeMessages(CNode* pfrom, std::string strCommand, CDataStream& vRecv)
{
    //probably one the extensions
    // obfuScationPool.ProcessMessageObfuscation(pfrom, strCommand, vRecv);
    mnodeman.ProcessMessage(activeMasternode,masternodeSync,pfrom, strCommand, vRecv);
    // budget.ProcessMessage(pfrom, strCommand, vRecv);
    masternodePayments.ProcessMessageMasternodePayments(masternodeSync,pfrom, strCommand, vRecv);
    // ProcessMessageSwiftTX(pfrom, strCommand, vRecv);
    masternodeSync.ProcessMessage(pfrom, strCommand, vRecv);
}

bool VoteForMasternodePayee(const CBlockIndex* pindex)
{
    if (masternodeSync.RequestedMasternodeAssets <= MASTERNODE_SYNC_LIST || !fMasterNode) return false;
    constexpr int numberOfBlocksIntoTheFutureToVoteOn = 10;
    static int64_t lastProcessBlockHeight = 0;
    const int64_t nBlockHeight = pindex->nHeight + numberOfBlocksIntoTheFutureToVoteOn;

    //reference node - hybrid mode

    uint256 seedHash;
    if (!GetBlockHashForScoring(seedHash, pindex, numberOfBlocksIntoTheFutureToVoteOn)) {
        LogPrint("mnpayments", "CMasternodePayments::ProcessBlock - failed to compute seed hash\n");
        return false;
    }

    const unsigned n = mnodeman.GetMasternodeRank(activeMasternode.vin, seedHash, ActiveProtocol(), CMasternodePayments::MNPAYMENTS_SIGNATURES_TOTAL);

    if (n == static_cast<unsigned>(-1)) {
        LogPrint("mnpayments", "CMasternodePayments::ProcessBlock - Unknown Masternode\n");
        return false;
    }

    if (n > CMasternodePayments::MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CMasternodePayments::ProcessBlock - Masternode not in the top %d (%d)\n", CMasternodePayments::MNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    if (nBlockHeight <= lastProcessBlockHeight) return false;

    CMasternodePaymentWinner newWinner(activeMasternode.vin, nBlockHeight, seedHash);

    LogPrint("masternode","CMasternodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeMasternode.vin.prevout.hash.ToString());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    CMasternode* pmn = mnodeman.GetNextMasternodeInQueueForPayment(pindex, numberOfBlocksIntoTheFutureToVoteOn, true);

    if (pmn != NULL) {
        LogPrint("masternode","CMasternodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

        newWinner.AddPayee(pmn->GetPaymentScript());
        LogPrint("masternode","CMasternodePayments::ProcessBlock() Winner %s nHeight %d. \n", pmn->vin.ToString(), newWinner.GetHeight());
    } else {
        LogPrint("masternode","CMasternodePayments::ProcessBlock() Failed to find masternode to pay\n");
    }

    LogPrint("masternode","CMasternodePayments::ProcessBlock() - Signing Winner\n");
    if(masternodePayments.CanVote(newWinner.vinMasternode.prevout,seedHash) && activeMasternode.SignMasternodeWinner(newWinner))
    {
        LogPrint("masternode","CMasternodePayments::ProcessBlock() - AddWinningMasternode\n");

        if (masternodePayments.AddWinningMasternode(newWinner)) {
            newWinner.Relay();
            lastProcessBlockHeight = nBlockHeight;
            return true;
        }
    }
    else
    {
        LogPrint("masternode","%s - Error signing masternode winner\n", __func__);
    }


    return false;
}

void LockUpMasternodeCollateral(const Settings& settings, std::function<void(const COutPoint&)> walletUtxoLockingFunction)
{
    if(settings.GetBoolArg("-mnconflock", true))
    {
        uint256 mnTxHash;
        BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries())
        {
            LogPrintf("  %s %s\n", mne.getTxHash(), mne.getOutputIndex());
            mnTxHash.SetHex(mne.getTxHash());
            COutPoint outpoint(mnTxHash, boost::lexical_cast<unsigned int>(mne.getOutputIndex()));
            walletUtxoLockingFunction(outpoint);
        }
    }
}

bool SetupActiveMasternode(const Settings& settings, std::string& errorMessage)
{
    if(!activeMasternode.SetMasternodeAddress(settings.GetArg("-masternodeaddr", "")))
    {
        errorMessage = "Invalid -masternodeaddr address: " + settings.GetArg("-masternodeaddr", "");
        return false;
    }
    LogPrintf("Masternode address: %s\n", activeMasternode.service.ToString());

    if(settings.ParameterIsSet("-masternodeprivkey"))
    {
        if(!activeMasternode.SetMasternodeKey(settings.GetArg("-masternodeprivkey", "")))
        {
            errorMessage = translate("Invalid masternodeprivkey. Please see documenation.");
            return false;
        }
    }
    else
    {
        errorMessage = translate("You must specify a masternodeprivkey in the configuration. Please see documentation for help.");
        return false;
    }
    return true;
}

//TODO: Rename/move to core
void ThreadMasternodeBackgroundSync()
{
    if (fLiteMode) return;

    RenameThread("divi-obfuscation");
    static const bool regtest = Params().NetworkID() == CBaseChainParams::REGTEST;

    int64_t nTimeManageStatus = 0;
    int64_t nTimeConnections = 0;
    int64_t lastResyncMasternodeData = GetTime();
    constexpr int64_t forceMasternodeResyncTimeWindow = 60*60;

    while (true) {
        int64_t now;
        {
            boost::unique_lock<boost::mutex> lock(csMockTime);
            cvMockTimeChanged.wait_for(lock, boost::chrono::seconds(1));
            now = GetTime();
        }

        // try to sync from all available nodes, one step at a time
        //
        // this function keeps track of its own "last call" time and
        // ignores calls if they are too early
        masternodeSync.Process(regtest);

        bool blockchainIsSynced = CMasternodeSync::IsBlockchainSynced();
        if(now - lastResyncMasternodeData > forceMasternodeResyncTimeWindow)
        {
            masternodeSync.Reset();
            lastResyncMasternodeData = now;
        }
        if(!blockchainIsSynced) continue;
        // check if we should activate or ping every few minutes,
        // start right after sync is considered to be done
        if (now >= nTimeManageStatus + MASTERNODE_PING_SECONDS) {
            nTimeManageStatus = now;
            activeMasternode.ManageStatus(mnodeman);
        }

        if (now >= nTimeConnections + 60) {
            nTimeConnections = now;
            mnodeman.CheckAndRemoveInnactive(masternodeSync);
            mnodeman.ProcessMasternodeConnections();
            masternodePayments.PruneOldMasternodeWinnerData(masternodeSync);
        }
    }
}
