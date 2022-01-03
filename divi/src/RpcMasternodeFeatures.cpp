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
#include <MasternodePaymentData.h>
#include <sync.h>
#include <StoredMasternodeBroadcasts.h>
#include <timedata.h>
#include <utilstrencodings.h>
#include <MasternodeHelpers.h>
#include <chain.h>
#include <script/standard.h>

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
    if (!mnodeman.ProcessMNBroadcastsAndPings(nullptr, "mnb", reserializedBroadcast))
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

bool SignMasternodeBroadcast(const CKeyStore& keystore, std::string& hexData)
{
    CMasternodeBroadcast mnb = readFromHex<CMasternodeBroadcast>(hexData);
    if(!keystore.HaveKey(mnb.pubKeyCollateralAddress.GetID()))
    {
        LogPrintf("%s - unknown key\n",__func__);
        return false;
    }
    CKey collateralKey;
    if(!keystore.GetKey(mnb.pubKeyCollateralAddress.GetID(),collateralKey))
    {
        LogPrintf("%s - unable to get key\n",__func__);
        return false;
    }
    std::string errorMessage;
    if(!CMasternodeBroadcastFactory::signBroadcast(collateralKey,mnb,errorMessage))
    {
        LogPrintf("%s - failed to sign. %s\n",__func__,errorMessage);
        return false;
    }
    CDataStream serializedBroadcast(SER_NETWORK,PROTOCOL_VERSION);
    serializedBroadcast << mnb;
    hexData = HexStr(serializedBroadcast.str());
    return true;
}

MasternodeStartResult RelayMasternodeBroadcast(const std::string& hexData, const std::string& signature, const bool updatePing)
{
    CMasternodeBroadcast mnb = readFromHex<CMasternodeBroadcast>(hexData);
    if(!signature.empty())
        mnb.signature = ParseHex(signature);

    return RelayParsedMasternodeBroadcast(mnb, updatePing);
}

MasternodeStartResult StartMasternode(const CKeyStore& keyStore, const StoredMasternodeBroadcasts& stored, std::string alias, bool deferRelay)
{
    const auto& mnModule = GetMasternodeModule();

    MasternodeStartResult result;
    for(const auto& configEntry : mnModule.getMasternodeConfigurations().getEntries())
    {
        if(configEntry.getAlias() != alias)
            continue;

        CMasternodeBroadcast mnb;
        bool updatePing = false;

        if(mnModule.localNodeIsAMasternode())
        {
            /* We failed to sign a new broadcast with our wallet, but we may
            have a stored one.  */

            COutPoint outp;
            if (!configEntry.parseInputReference(outp))
            {
                result.status = false;
                result.errorMessage = "Failed to parse input reference";
                return result;
            }

            if (!stored.GetBroadcast(outp, mnb))
            {
                result.status = false;
                result.errorMessage = "No broadcast message available";
                return result;
            }

            updatePing = true;
        }
        else
        {
            result.status = false;
            result.errorMessage = "Could not create MN broadcast. Check your masternode.conf file";
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

        return RelayParsedMasternodeBroadcast (mnb, updatePing);
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

    CMasternode mn;
    if (mnodeman.GetMNCopy(activeMasternode.vin,mn)) {
        result.txHash = activeMasternode.vin.prevout.hash.ToString();
        result.outputIndex = std::to_string(activeMasternode.vin.prevout.n);
        result.netAddress = activeMasternode.service.ToString();
        result.collateralAddress = CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString();
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

unsigned FindLastPayeePaymentTime(const CBlockIndex* chainTip, const MasternodePaymentData& paymentData, const CMasternode& masternode, const unsigned maxBlockDepth)
{
    assert(chainTip);
    CScript mnPayee = GetScriptForDestination(masternode.pubKeyCollateralAddress.GetID());
    unsigned n = 0;
    for (unsigned int i = 1; chainTip && chainTip->nHeight > 0; i++) {
        if (n >= maxBlockDepth) {
            return 0u;
        }
        n++;

        uint256 scoringBlockHash;
        if (!GetBlockHashForScoring(scoringBlockHash, chainTip, 0))
            continue;

        auto* masternodePayees = paymentData.getPayeesForScoreHash(scoringBlockHash);
        if (masternodePayees != nullptr) {
            /*
                Search for this payee, with at least 2 votes. This will aid in consensus allowing the network
                to converge on the same payees quickly, then keep the same schedule.
            */
            if (masternodePayees->HasPayeeWithVotes(mnPayee, 2)) {
                return chainTip->nTime + masternode.DeterministicTimeOffset();
            }
        }

        if (chainTip->pprev == NULL) {
            assert(chainTip);
            break;
        }
        chainTip = chainTip->pprev;
    }
    return 0u;
}

std::vector<MasternodeListEntry> GetMasternodeList(std::string strFilter, const CBlockIndex* chainTip)
{
    const auto& mnModule = GetMasternodeModule();
    auto& networkMessageManager = mnModule.getNetworkMessageManager();
    const MasternodePaymentData& paymentData = mnModule.getMasternodePaymentData();

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
        entry.lastPaidTime = (int64_t) FindLastPayeePaymentTime(chainTip,paymentData,masternode,numberOfBlocksToSearchBackForLastPayment);
        entry.masternodeTier = CMasternode::TierToString(static_cast<MasternodeTier>(masternode.nTier));
    }
    return masternodeList;
}

static int StableMasternodeCount()
{
    const auto& mnModule = GetMasternodeModule();
    auto& networkMessageManager = mnModule.getNetworkMessageManager();
    AssertLockHeld(networkMessageManager.cs_process_message);
    int nStable_size = 0;
    int nMinProtocol = ActiveProtocol();
    int64_t nMasternode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nMasternode_Age = 0;

    mnModule.getMasternodeManager().Check();
    for (auto& mn : networkMessageManager.masternodes) {
        if (mn.protocolVersion < nMinProtocol) {
            continue; // Skip obsolete versions
        }
        nMasternode_Age = GetAdjustedTime() - mn.sigTime;
        if ((nMasternode_Age) < nMasternode_Min_Age) {
            continue; // Skip masternodes younger than (default) 8000 sec (MUST be > MASTERNODE_REMOVAL_SECONDS)
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

    mnModule.getMasternodeManager().Check();
    AssertLockHeld(networkMessageManager.cs_process_message);
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

static unsigned CountEnabled(const std::vector<CMasternode>& masternodes)
{
    const int protocolVersion = ActiveProtocol();
    unsigned count = 0u;
    for (const CMasternode& mn : masternodes) {
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        ++count;
    }

    return count;
}

MasternodeCountData GetMasternodeCounts(const CBlockIndex* chainTip)
{
    const auto& mnModule = GetMasternodeModule();
    auto& networkMessageManager = mnModule.getNetworkMessageManager();
    auto& masternodePayments = mnModule.getMasternodePayments();

    MasternodeCountData data;
    if (chainTip != nullptr)
        data.queueCount = masternodePayments.GetMasternodePaymentQueue(chainTip, 0).queueSize;

    {
        LOCK(networkMessageManager.cs_process_message);
        CountNetworks(data.ipv4, data.ipv6, data.onion);
        data.stable = StableMasternodeCount();
        data.enabledAndActive = CountEnabled(networkMessageManager.masternodes);
        data.total = networkMessageManager.masternodes.size();
    }
    data.enabled = data.enabledAndActive;

    return data;
}
