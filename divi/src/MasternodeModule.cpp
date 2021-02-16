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
#include <base58address.h>
#include <flat-database.h>
#include <uiMessenger.h>
#include <MasternodeNetworkMessageManager.h>
#include <MasternodePaymentData.h>
#include <MasternodeHelpers.h>
#include <MasternodeBroadcastFactory.h>

#include <addrman.h>
#include <blockmap.h>

bool fMasterNode = false;
extern bool fLiteMode;
extern CChain chainActive;
extern BlockMap mapBlockIndex;
MasternodeNetworkMessageManager networkMessageManager;
MasternodePaymentData masternodePaymentData;
CMasternodeConfig masternodeConfig;
CMasternodeMan mnodeman(networkMessageManager,chainActive,mapBlockIndex,addrman);
CMasternodeSync masternodeSync(mnodeman, networkMessageManager,masternodePaymentData);
CActiveMasternode activeMasternode(masternodeConfig, fMasterNode);
CMasternodePayments masternodePayments(masternodePaymentData,mnodeman);

template <typename T>
static T readFromHex(std::string hexString)
{
    std::vector<unsigned char> hex = ParseHex(hexString);
    CDataStream ss(hex,SER_NETWORK,PROTOCOL_VERSION);
    T object;
    ss >> object;
    return object;
}

bool InitializeMasternodeIfRequested(const Settings& settings, bool transactionIndexEnabled, std::string& errorMessage)
{
    fMasterNode = settings.GetBoolArg("-masternode", false);
    fLiteMode = settings.GetBoolArg("-litemode", false);
    if (fMasterNode && fLiteMode) {
        errorMessage = "You can not start a masternode in litemode";
        return false;
    }
    if(!LoadMasternodeConfigurations(errorMessage))
    {
        return false;
    }
    if (fMasterNode && transactionIndexEnabled == false)
    {
        errorMessage = "Enabling Masternode support requires turning on transaction indexing."
                         "Please add txindex=1 to your configuration and start with -reindex";
        return false;
    }
    else if (fMasterNode)
    {
        LogPrintf("IS MASTER NODE\n");
        if(!SetupActiveMasternode(settings,errorMessage))
        {
            return false;
        }
    }
    return true;
}

CMasternodePayments& GetMasternodePayments()
{
    return masternodePayments;
}

bool LoadMasternodeDataFromDisk(UIMessenger& uiMessenger,std::string pathToDataDir)
{
    if (!fLiteMode)
    {
        std::string strDBName;

        strDBName = "mncache.dat";
        uiMessenger.InitMessage("Loading masternode cache...");
        CFlatDB<MasternodeNetworkMessageManager> flatdb1(strDBName, "magicMasternodeCache");
        if(!flatdb1.Load(networkMessageManager)) {
            return uiMessenger.InitError("Failed to load masternode cache from", "\n" + pathToDataDir );
        }

        if(networkMessageManager.masternodeCount()) {
            strDBName = "mnpayments.dat";
            uiMessenger.InitMessage("Loading masternode payment cache...");
            CFlatDB<MasternodePaymentData> flatdb2(strDBName, "magicMasternodePaymentsCache");
            if(!flatdb2.Load(masternodePaymentData)) {
                return uiMessenger.InitError("Failed to load masternode payments cache from", "\n" + pathToDataDir);
            }
        } else {
            uiMessenger.InitMessage("Masternode cache is empty, skipping payments and governance cache...");
        }
    }
    return true;
}
void DumpMasternodeDataToDisk()
{
    if(!fLiteMode)
    {
        CFlatDB<MasternodeNetworkMessageManager> flatdb1("mncache.dat", "magicMasternodeCache");
        flatdb1.Dump(networkMessageManager);
        CFlatDB<MasternodePaymentData> flatdb2("mnpayments.dat", "magicMasternodePaymentsCache");
        flatdb2.Dump(masternodePaymentData);
    }
}

void CountNetworks(int& ipv4, int& ipv6, int& onion)
{
    int protocolVersion = ActiveProtocol();

    BOOST_FOREACH (CMasternode& mn, networkMessageManager.masternodes)
    {
        mn.Check();
        std::string strHost;
        int port;
        SplitHostPort(mn.addr.ToString(), port, strHost);
        CNetAddr node = CNetAddr(strHost, false);
        int nNetwork = node.GetNetwork();
        switch (nNetwork) {
            case 1 :
                ipv4++;
                break;
            case 2 :
                ipv6++;
                break;
            case 3 :
                onion++;
                break;
        }
    }
}

MasternodeCountData::MasternodeCountData(
    ): total(0)
    , stable(0)
    , enabledAndActive(0)
    , enabled(0)
    , queueCount(0)
    , ipv4(0)
    , ipv6(0)
    , onion(0)
{
}

MasternodeCountData GetMasternodeCounts(const CBlockIndex* chainTip)
{
    MasternodeCountData data;
    if (chainTip != nullptr)
        data.queueCount = masternodePayments.GetMasternodePaymentQueue(chainTip, 0).size();

    CountNetworks(data.ipv4, data.ipv6, data.onion);
    data.total = networkMessageManager.masternodeCount();
    data.stable = mnodeman.stable_size();
    data.enabledAndActive = mnodeman.CountEnabled();
    data.enabled = mnodeman.CountEnabled();

    return data;
}

std::vector<MasternodeListEntry> GetMasternodeList(std::string strFilter)
{
    std::vector<MasternodeListEntry> masternodeList;
    std::vector<CMasternode> masternodeVector = mnodeman.GetFullMasternodeVector();
    masternodeList.reserve(masternodeVector.size());

    unsigned numberOfBlocksToSearchBackForLastPayment = (masternodeVector.size()*5)/4;
    for(auto& masternode : masternodeVector)
    {
        std::string strVin = masternode.vin.prevout.ToStringShort();
        std::string strTxHash = masternode.vin.prevout.hash.ToString();
        uint32_t oIdx = masternode.vin.prevout.n;
        const std::string collateralAddress = CBitcoinAddress(masternode.pubKeyCollateralAddress.GetID()).ToString();
        if (strFilter != "" &&
            strTxHash.find(strFilter) == std::string::npos &&
            masternode.Status().find(strFilter) == std::string::npos &&
            collateralAddress.find(strFilter) == std::string::npos)
        {
            continue;
        }
        masternodeList.emplace_back();
        MasternodeListEntry& entry = masternodeList.back();

        std::string strStatus = masternode.Status();
        std::string strHost;
        int port;
        SplitHostPort(masternode.addr.ToString(), port, strHost);
        CNetAddr node = CNetAddr(strHost, false);
        std::string strNetwork = GetNetworkName(node.GetNetwork());

        entry.network = strNetwork;
        entry.txHash = strTxHash;
        entry.outputIndex = oIdx;
        entry.status = strStatus;
        entry.collateralAddress = CBitcoinAddress(masternode.pubKeyCollateralAddress.GetID()).ToString();
        entry.protocolVersion = masternode.protocolVersion;
        entry.lastSeenTime = (int64_t)masternode.lastPing.sigTime;
        entry.activeTime = (int64_t)(masternode.lastPing.sigTime - masternode.sigTime);
        entry.lastPaidTime = (int64_t)masternodePayments.FindLastPayeePaymentTime(masternode,numberOfBlocksToSearchBackForLastPayment);
        entry.masternodeTier = CMasternode::TierToString(static_cast<MasternodeTier>(masternode.nTier));
    }
    return masternodeList;
}

ActiveMasternodeStatus GetActiveMasternodeStatus()
{
    if (!fMasterNode) throw std::runtime_error("This is not a masternode");
    ActiveMasternodeStatus result;

    CMasternode* pmn = mnodeman.Find(activeMasternode.vin);

    if (pmn) {
        result.txHash = activeMasternode.vin.prevout.hash.ToString();
        result.outputIndex = std::to_string(activeMasternode.vin.prevout.n);
        result.netAddress = activeMasternode.service.ToString();
        result.collateralAddress = CBitcoinAddress(pmn->pubKeyCollateralAddress.GetID()).ToString();
        result.statusCode = std::to_string(activeMasternode.status);
        result.statusMessage = activeMasternode.GetStatus();
        result.activeMasternodeFound = true;
        return result;
    }
    assert(!result.activeMasternodeFound);
    return result;
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
    if (networkMessageManager.pingIsKnown(inventoryHash)) {
        peer->PushMessage("mnp", networkMessageManager.getKnownPing(inventoryHash));
        return true;
    }
    return false;
}

bool ShareMasternodeBroadcastWithPeer(CNode* peer,const uint256& inventoryHash)
{
    if (networkMessageManager.broadcastIsKnown(inventoryHash))
    {
        peer->PushMessage("mnb", networkMessageManager.getKnownBroadcast(inventoryHash));
        return true;
    }
    return false;
}
bool MasternodePingIsKnown(const uint256& inventoryHash)
{
    return networkMessageManager.pingIsKnown(inventoryHash);
}
bool MasternodeIsKnown(const uint256& inventoryHash)
{
    if (networkMessageManager.broadcastIsKnown(inventoryHash))
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
    if(!fLiteMode)
    {
        mnodeman.ProcessMessage(activeMasternode,masternodeSync,pfrom, strCommand, vRecv);
        masternodePayments.ProcessMessageMasternodePayments(masternodeSync,pfrom, strCommand, vRecv);
    }
    masternodeSync.ProcessMessage(pfrom, strCommand, vRecv);
}

bool VoteForMasternodePayee(const CBlockIndex* pindex)
{
    if (fLiteMode || masternodeSync.RequestedMasternodeAssets <= MASTERNODE_SYNC_LIST || !fMasterNode) return false;
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
    CScript payee = masternodePayments.GetNextMasternodePayeeInQueueForPayment(pindex, numberOfBlocksIntoTheFutureToVoteOn);

    if (!payee.empty()) {
        LogPrint("masternode","CMasternodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

        newWinner.AddPayee(payee);
        LogPrint("masternode","CMasternodePayments::ProcessBlock() WinnerPayee %s nHeight %d. \n", payee.ToString(), newWinner.GetHeight());
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

bool LoadMasternodeConfigurations(std::string& errorMessage)
{
    // parse masternode.conf
    if (!masternodeConfig.read(errorMessage)) {
        errorMessage="Error reading masternode configuration file: "+ errorMessage + "\n";
        return false;
    }
    return true;
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

        bool blockchainIsSynced = IsBlockchainSynced();
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
