#include <RpcMasternodeFeatures.h>

#include <base58address.h>
#include <MasternodeModule.h>
#include <masternode.h>
#include <keystore.h>
#include <streams.h>
#include <serialize.h>
#include <spork.h>
#include <activemasternode.h>
#include <Logging.h>
#include <masternodeman.h>
#include <masternodeconfig.h>
#include <MasternodeBroadcastFactory.h>
#include <MasternodeNetworkMessageManager.h>
#include <masternode-payments.h>
#include <sync.h>
#include <timedata.h>
#include <utilstrencodings.h>
#include <MasternodeHelpers.h>

#define MN_WINNER_MINIMUM_AGE 8000    // Age in seconds. This should be > MASTERNODE_REMOVAL_SECONDS to avoid misconfigured new nodes in the list.
template <typename T>
static T readFromHex(std::string hexString)
{
    std::vector<unsigned char> hex = ParseHex(hexString);
    CDataStream ss(hex,SER_NETWORK,PROTOCOL_VERSION);
    T object;
    ss >> object;
    return object;
}

MasternodeStartResult::MasternodeStartResult(
    ): status(false)
    , broadcastData("")
    , errorMessage("No Error Data")
{}
ActiveMasternodeStatus::ActiveMasternodeStatus(
    ): activeMasternodeFound(false)
    , txHash()
    , outputIndex()
    , netAddress()
    , collateralAddress()
    , statusCode()
    , statusMessage()
{}
MasternodeListEntry::MasternodeListEntry(
    ): network()
    , txHash()
    , outputIndex()
    , status()
    , collateralAddress()
    , protocolVersion()
    , signatureTime()
    , lastSeenTime()
    , activeTime()
    , lastPaidTime()
    , masternodeTier()
{}
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

namespace
{

/** Relays an already parsed masternode broadcast, optionally updating the
 *  contained ping if we can (because it is our masternode).  */
MasternodeStartResult RelayParsedMasternodeBroadcast(CMasternodeBroadcast mnb, const bool updatePing)
{
    const auto& mnModule = GetMasternodeModule();
    auto& activeMasternode = mnModule.getActiveMasternode();
    auto& mnodeman = mnModule.getMasternodeManager();

    if (updatePing)
    {
        if(activeMasternode.IsOurBroadcast(mnb,true))
        {
            if(activeMasternode.UpdatePing(mnb.lastPing))
                LogPrint("masternode", "Ping updated successfully!\n");
            else
                LogPrint("masternode", "Ping not updated! Failure to sign!\n");
        }
        else
            LogPrint("masternode", "This broadcast does not belong to us!\n");
    }

    if(!IsBlockchainSynced())
    {
        LogPrintf("Warning! Trying to relay broadcast while blockchain sync hasnt completed may fail!\n");
    }

    MasternodeStartResult result;

    CDataStream reserializedBroadcast(SER_NETWORK,PROTOCOL_VERSION);
    reserializedBroadcast << mnb;
    if (!mnodeman.ProcessMessage(nullptr, "mnb", reserializedBroadcast))
    {
        LogPrintf("%s - Relaying broadcast vin = %s\n",__func__, mnb.vin);
        result.status = false;
        result.errorMessage = "Error processing broadcast";
        return result;
    }

    result.status = true;
    return result;
}

} // anonymous namespace

MasternodeStartResult RelayMasternodeBroadcast(const std::string& hexData, const std::string& signature, const bool updatePing)
{
    CMasternodeBroadcast mnb = readFromHex<CMasternodeBroadcast>(hexData);
    if(!signature.empty())
        mnb.signature = ParseHex(signature);

    return RelayParsedMasternodeBroadcast(mnb, updatePing);
}

MasternodeStartResult StartMasternode(const CKeyStore& keyStore, std::string alias, bool deferRelay)
{
    const auto& mnModule = GetMasternodeModule();
    auto& mnodeman = mnModule.getMasternodeManager();

    MasternodeStartResult result;
    for(const auto& configEntry : mnModule.getMasternodeConfigurations().getEntries())
    {
        if(configEntry.getAlias() != alias)
            continue;

        CMasternodeBroadcast mnb;

        if(!CMasternodeBroadcastFactory::Create(
                keyStore,
                configEntry,
                result.errorMessage,
                mnb,
                false,
                deferRelay))
        {
            result.status = false;
            return result;
        }

        CDataStream serializedBroadcast(SER_NETWORK,PROTOCOL_VERSION);
        serializedBroadcast << mnb;
        if (deferRelay)
        {
            result.status = true;
            result.broadcastData = HexStr(serializedBroadcast.str());
            return result;
        }

        return RelayParsedMasternodeBroadcast (mnb, false);
    }
    result.status = false;
    result.errorMessage = "Invalid alias, couldn't find MN. Check your masternode.conf file";
    return result;
}

ActiveMasternodeStatus GetActiveMasternodeStatus()
{
    const auto& mnModule = GetMasternodeModule();
    if (!mnModule.localNodeIsAMasternode()) throw std::runtime_error("This is not a masternode");
    auto& activeMasternode = mnModule.getActiveMasternode();
    auto& mnodeman = mnModule.getMasternodeManager();

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
    else
    {
        result.statusMessage = activeMasternode.GetStatus();
        result.activeMasternodeFound = false;
        return result;
    }
}

std::vector<MasternodeListEntry> GetMasternodeList(std::string strFilter)
{
    const auto& mnModule = GetMasternodeModule();
    auto& networkMessageManager = mnModule.getNetworkMessageManager();
    auto& masternodePayments = mnModule.getMasternodePayments();

    std::vector<MasternodeListEntry> masternodeList;
    std::vector<CMasternode> masternodeVector = networkMessageManager.GetFullMasternodeVector();
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

static int StableMasternodeCount()
{
    const auto& mnModule = GetMasternodeModule();
    auto& networkMessageManager = mnModule.getNetworkMessageManager();
    LOCK(networkMessageManager.cs);
    int nStable_size = 0;
    int nMinProtocol = ActiveProtocol();
    int64_t nMasternode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nMasternode_Age = 0;

    mnModule.getMasternodeManager().Check();
    for (auto& mn : networkMessageManager.masternodes) {
        if (mn.protocolVersion < nMinProtocol) {
            continue; // Skip obsolete versions
        }
        if (GetSporkManager().IsSporkActive (SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
            nMasternode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nMasternode_Age) < nMasternode_Min_Age) {
                continue; // Skip masternodes younger than (default) 8000 sec (MUST be > MASTERNODE_REMOVAL_SECONDS)
            }
        }
        if (!mn.IsEnabled ())
            continue; // Skip not-enabled masternodes

        nStable_size++;
    }

    return nStable_size;
}


static void CountNetworks(int& ipv4, int& ipv6, int& onion)
{
    const auto& mnModule = GetMasternodeModule();
    auto& networkMessageManager = mnModule.getNetworkMessageManager();
    int protocolVersion = ActiveProtocol();

    mnModule.getMasternodeManager().Check();
    LOCK(networkMessageManager.cs);
    for(CMasternode& mn: networkMessageManager.masternodes)
    {
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

MasternodeCountData GetMasternodeCounts(const CBlockIndex* chainTip)
{
    const auto& mnModule = GetMasternodeModule();
    auto& networkMessageManager = mnModule.getNetworkMessageManager();
    auto& masternodePayments = mnModule.getMasternodePayments();
    auto& mnodeman = mnModule.getMasternodeManager();

    MasternodeCountData data;
    if (chainTip != nullptr)
        data.queueCount = masternodePayments.GetMasternodePaymentQueue(chainTip, 0).size();

    CountNetworks(data.ipv4, data.ipv6, data.onion);
    data.total = networkMessageManager.masternodeCount();
    data.stable = StableMasternodeCount();
    data.enabledAndActive = mnodeman.CountEnabled();
    data.enabled = data.enabledAndActive;

    return data;
}
