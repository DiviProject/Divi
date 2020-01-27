// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <db.h>
#include <init.h>
#include <masternodes/activemasternode.h>
#include <masternodes/masternodeman.h>
#include <masternodes/masternode-payments.h>
#include <masternodes/masternode-sync.h>
#include <masternodes/masternodeconfig.h>
#include <key_io.h>
#include <script/standard.h>
#include <rpc/server.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>
#include <wallet/coincontrol.h>
#include <netbase.h>

#include <vector>
#include <memory>
#include <string>
#include <boost/tokenizer.hpp>
#include <boost/algorithm/string.hpp>
#include <univalue.h>
#include <fstream>

template <typename T>
static T readFromHex(std::string hexString)
{
    std::vector<unsigned char> hex = ParseHex(hexString);
    CDataStream ss(hex,SER_NETWORK,PROTOCOL_VERSION);
    T object;
    ss >> object;
    return object;
}

static CMasternode::Tier GetMasternodeTierFromString(std::string str)
{
    boost::algorithm::to_lower(str); // modifies str

    if(str == "copper") {
        return CMasternode::MASTERNODE_TIER_COPPER;
    }
    else if(str == "silver") {
        return CMasternode::MASTERNODE_TIER_SILVER;
    }
    else if(str == "gold") {
        return CMasternode::MASTERNODE_TIER_GOLD;
    }
    else if(str == "platinum") {
        return CMasternode::MASTERNODE_TIER_PLATINUM;
    }
    else if(str == "diamond") {
        return CMasternode::MASTERNODE_TIER_DIAMOND;
    }

    return CMasternode::MASTERNODE_TIER_INVALID;
}

static CTxDestination GetOrGenerateAddressForLabel(CWallet *pwallet, std::string label)
{
    AssertLockHeld(pwallet->cs_wallet);
    std::set<CTxDestination> setAddresses = pwallet->GetLabelAddresses(label);
    if(setAddresses.empty())
    {
        if (!pwallet->IsLocked()) {
            pwallet->TopUpKeyPool();
        }

        // Generate a new key that is added to wallet
        CPubKey newKey;
        if (!pwallet->GetKeyFromPool(newKey)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
        }
        auto output_type = OutputType::LEGACY;
        pwallet->LearnRelatedScripts(newKey, output_type);
        CTxDestination dest = GetDestinationForKey(newKey, output_type);

        pwallet->SetAddressBook(dest, label, "receive");
        return dest;
    }
    else
    {
        return *setAddresses.begin();
    }
}

static UniValue allocatefunds(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> wallet = GetWalletForJSONRPCRequest(request);
    CWallet* pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 3)
        throw runtime_error(
                "allocatefunds purpose alias amount ( \"pay wallet\" ( \"voting wallet\" ) )\n"
                "\nStarts escrows funds for some purpose.\n"

                "\nArguments:\n"
                "1. purpose			(string, required) Helpful identifier to recognize this allocation later.  Currently only masternode is recognized. \n"
                "2. identifier      (string, required) Helpful Identifier to recognize the specific instance of the funding type that the funding is for. \n"
                "3. amount			(diamond, platinum, gold, silver, copper) tier of masternode. \n"
                "      <future>     (numeric, required) amount of divi funded will also be accepted for partially funding master nodes and other purposes.\n"

                "\nResult:\n"
                "\"vin\"			(string) funding transaction id necessary for next step.\n");

    if (request.params[0].get_str() != "masternode")
    {
        throw runtime_error("Surely you meant the first argument to be ""masternode"" . . . . ");
    }
    LOCK(pwallet->cs_wallet);
    auto label = "alloc->" + request.params[1].get_str();
    string strAmt = request.params[2].get_str();
    auto nMasternodeTier = GetMasternodeTierFromString(strAmt);
    if(!CMasternode::IsTierValid(nMasternodeTier))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid masternode tier");
    }
        
    auto acctAddr = GetOrGenerateAddressForLabel(pwallet, label);

    auto locked_chain = pwallet->chain().lock();
    CCoinControl coinControl;
    CTransactionRef txRef = SendMoney(*locked_chain, pwallet, acctAddr, CMasternode::GetTierCollateralAmount(nMasternodeTier), false, coinControl, {});

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("txhash", txRef->GetHash().GetHex());

    return obj;
}

static UniValue fundmasternode(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> wallet = GetWalletForJSONRPCRequest(request);
    CWallet* pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 4)
        throw runtime_error(
                "fundmasternode alias amount TxID masternode ( \"pay wallet\" ( \"voting wallet\" ) )\n"
                "\nVerifies the escrowed funds for the masternode and returns the necessary info for your and its configuration files.\n"

                "\nArguments:\n"
                "1. alias			(string, required) helpful identifier to recognize this allocation later.\n"
                "2. amount			(diamond, platinum, gold, silver, copper) tier of masternode. \n"
                "      <future>     (numeric, required) amount of divi funded will also be accepted for partially funding master nodes and other purposes.\n"
                "3. TxID			(string, required) funding transaction id .\n"
                "4. masternode		(string, required) ip address of masternode.\n"
                "(use an empty string for the pay wallet if the same as the funding wallet and you wish to assign a different voting wallet).\n"

                "\nResult:\n"
                "\"config line\"	(string) the above details for the masternode & wallet config files & cryptographic signature proving that you authorized this.\n");

    auto alias = request.params[0].get_str();
    auto nMasternodeTier = GetMasternodeTierFromString(request.params[1].get_str());

    if(!CMasternode::IsTierValid(nMasternodeTier))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid masternode tier");
    }

    uint256 txHash;
    txHash.SetHex(request.params[2].get_str());
    std::string mnAddress = request.params[3].get_str();

    bool found = false;
    auto nAmount = CMasternode::GetTierCollateralAmount(nMasternodeTier);
    bool outputIndex = -1;

    LOCK(pwallet->cs_wallet);
    if(auto wtx = pwallet->GetWalletTx(txHash))
    {
        auto locked_chain = pwallet->chain().lock();
        for(size_t i = 0; i < wtx->tx->vout.size(); ++i)
        {
            if(wtx->tx->vout[i].nValue == nAmount && !pwallet->IsSpent(*locked_chain, txHash, i))
            {
                found = true;
                outputIndex = i;
                break;
            }
        }
    }

    if (!found) {
        throw JSONRPCError(RPC_VERIFY_ERROR, "Couldn't verify transaction");
    }

    if (!pwallet->IsLocked())
    {
        pwallet->TopUpKeyPool();
    }    

    // Generate a new key that is added to wallet
    auto label = "reserved->" + alias;

    auto address = GetOrGenerateAddressForLabel(pwallet, label);

    CKeyID *keyID = boost::get<CKeyID>(&address);
    if (keyID == nullptr)
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");

    CKey vchSecret;
    if (!pwallet->GetKey(*keyID, vchSecret))
    {    
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + EncodeDestination(address) + " is not known");
    }

    auto tokens = {
        alias,
        mnAddress + ":" + std::to_string(Params().GetDefaultPort()),
        EncodeSecret(vchSecret),
        txHash.ToString(),
        std::to_string(outputIndex)
    };

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("config line", boost::algorithm::join(tokens, " "));
    return obj;
}

// TODO fix method
static UniValue setupmasternode(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> wallet = GetWalletForJSONRPCRequest(request);
    CWallet* pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    const auto& params = request.params;

	if (request.fHelp || params.size() != 5)
		throw runtime_error(
			"setupmasternode alias txin outputidx collateralPubKey ipaddress\n"
			"\nStarts escrows funds for some purpose.\n"

			"\nArguments:\n"
			"1. alias			    (string, required) Helpful identifier to recognize this masternode later. \n"
			"2. txHash              (string, required) Funding transaction. \n"
            "3. outputIndex         (string, required) Output index transaction. \n"
            "4. collateralPubkey    (string, required) collateral pubkey. \n"
            "5. ip_address          (string, required) Local ip address of this node\n"
			"\nResult:\n"
			"\"vin\"			(string) funding transaction id necessary for next step.\n");

    UniValue result(UniValue::VOBJ);

    std::string label = "reserved->" + params[0].get_str();
    auto address = GetOrGenerateAddressForLabel(pwallet, label);
    CKeyID *keyID = boost::get<CKeyID>(&address);
    if (keyID == nullptr)
    {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    }
    CKey masternodeKey;
    if (!pwallet->GetKey(*keyID, masternodeKey))
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + EncodeDestination(*keyID) + " is not known");
    }
    
    std::string alias = params[0].get_str();
    std::string txHash = params[1].get_str();
    std::string outputIndex = params[2].get_str();
    std::vector<unsigned char> pubkeyStr = ParseHex(static_cast<std::string>(params[3].get_str()));
    CPubKey pubkeyCollateralAddress;
    pubkeyCollateralAddress.Set(pubkeyStr.begin(),pubkeyStr.end());

    std::string ip = params[4].get_str();

    CMasternodeConfig::CMasternodeEntry config(alias,ip,EncodeSecret(masternodeKey),txHash,outputIndex);

    CMasternodeBroadcast mnb;
    std::string errorMsg;
    if(!CMasternodeBroadcastFactory::Create(*pwallet,config,pubkeyCollateralAddress,errorMsg,mnb))
    {
        throw JSONRPCError(RPC_INVALID_PARAMS,errorMsg);
    }

    CDataStream ss(SER_NETWORK,PROTOCOL_VERSION);
    result.pushKV("protocol_version", PROTOCOL_VERSION );
    result.pushKV("message_to_sign", HexStr(mnb.getMessageToSign()) );
    ss << mnb;
    result.pushKV("broadcast_data", HexStr(ss.str()) );
    return result;    
}

string nodeHelp(string indent = "")
{
    string ret = indent + "\"address\": \"address\",    (string) Masternode DIVI address\n";
    ret += indent + "\"protocol\": xxxx,        (numeric) Protocol version\n";
    //	ret += indent + "\"netaddr\": \"xxxx\",       (string) Masternode network address\n";;
    ret += indent + "\"lastseen\": ttt,			(numeric) The time in seconds since last seen\n";
    ret += indent + "\"activetime\": ttt,		(numeric) The time in seconds masternode has been active\n";
    ret += indent + "\"lastpaid\": ttt,			(numeric) The time in seconds since masternode was last paid\n";
    return ret;
}

static UniValue listmasternodes(const JSONRPCRequest& request)
{
    std::string strFilter = "";

    if (request.params.size() == 1) strFilter = request.params[0].get_str();

    if (request.fHelp || (request.params.size() > 1))
        throw runtime_error(
                "listmasternodes ( \"filter\" )\n"
                "\nGet a ranked list of masternodes\n"

                "\nArguments:\n"
                "1. \"filter\"    (string, optional) Filter search text. Partial match by txhash, status, or addr.\n"

                "\nResult:\n"
                "[\n"
                "  {\n"
                "    \"rank\": n,           (numeric) Masternode Rank (or 0 if not enabled)\n"
                "    \"txhash\": \"hash\",    (string) Collateral transaction hash\n"
                "    \"outidx\": n,         (numeric) Collateral transaction output index\n"
                "    \"status\": s,         (string) Status (ENABLED/EXPIRED/REMOVE/etc)\n"
                "    \"addr\": \"addr\",      (string) Masternode PIVX address\n"
                "    \"version\": v,        (numeric) Masternode protocol version\n"
                "    \"lastseen\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last seen\n"
                "    \"activetime\": ttt,   (numeric) The time in seconds since epoch (Jan 1 1970 GMT) masternode has been active\n"
                "    \"lastpaid\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) masternode was last paid\n"
                "  }\n"
                "  ,...\n"
                "]\n"
                "\nExamples:\n" +
                HelpExampleCli("masternodelist", "") + HelpExampleRpc("masternodelist", ""));

    UniValue ret(UniValue::VARR);
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if(!pindex) return 0;
    }
    for(auto &&entry : mnodeman.GetFullMasternodeVector()) {
        UniValue obj(UniValue::VOBJ);
        std::string strVin = entry.vin.prevout.ToStringShort();
        std::string strTxHash = entry.vin.prevout.hash.ToString();
        uint32_t oIdx = entry.vin.prevout.n;

        CMasternode* mn = mnodeman.Find(entry.vin);

        if (mn != NULL) {
            if (strFilter != "" && strTxHash.find(strFilter) == string::npos &&
                    mn->Status().find(strFilter) == string::npos &&
                    EncodeDestination(mn->pubKeyCollateralAddress.GetID()).find(strFilter) == string::npos) continue;

            std::string strStatus = mn->Status();
            std::string strHost;
            int port;
            SplitHostPort(mn->addr.ToString(), port, strHost);
            CNetAddr node;
            LookupHost(strHost.c_str(), node, false);
            std::string strNetwork = GetNetworkName(node.GetNetwork());

            obj.pushKV("network", strNetwork);
            obj.pushKV("txhash", strTxHash);
            obj.pushKV("outidx", (uint64_t)oIdx);
            obj.pushKV("status", strStatus);
            obj.pushKV("addr", EncodeDestination(mn->pubKeyCollateralAddress.GetID()));
            obj.pushKV("version", mn->protocolVersion);
            obj.pushKV("lastseen", (int64_t)mn->lastPing.sigTime);
            obj.pushKV("activetime", (int64_t)(mn->lastPing.sigTime - mn->sigTime));
            obj.pushKV("lastpaid", (int64_t)mn->GetLastPaid());
            obj.pushKV("tier", CMasternode::TierToString(static_cast<CMasternode::Tier>(mn->nTier)));

            ret.push_back(obj);
        }
    }

    return ret;
}

static UniValue masternodeconnect(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 1))
        throw runtime_error(
                "masternodeconnect \"address\"\n"
                "\nAttempts to connect to specified masternode address\n"

                "\nArguments:\n"
                "1. \"address\"     (string, required) IP or net address to connect to\n"

                "\nExamples:\n" +
                HelpExampleCli("masternodeconnect", "\"192.168.0.6:51472\"") + HelpExampleRpc("masternodeconnect", "\"192.168.0.6:51472\""));

    std::string strAddress = request.params[0].get_str();


    CService addr;
    Lookup(strAddress.c_str(), addr, 0, false);

    if(g_connman->OpenNetworkConnection(CAddress(addr, NODE_NETWORK), false))
    {
        return UniValue();
    }
    else
    {
        throw runtime_error("error connecting\n");
    }
}

static UniValue getmasternodecount (const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() > 0))
        throw runtime_error(
                "getmasternodecount\n"
                "\nGet masternode count values\n"

                "\nResult:\n"
                "{\n"
                "  \"total\": n,        (numeric) Total masternodes\n"
                "  \"stable\": n,       (numeric) Stable count\n"
                "  \"obfcompat\": n,    (numeric) Obfuscation Compatible\n"
                "  \"enabled\": n,      (numeric) Enabled masternodes\n"
                "  \"inqueue\": n       (numeric) Masternodes in queue\n"
                "}\n"
                "\nExamples:\n" +
                HelpExampleCli("getmasternodecount", "") + HelpExampleRpc("getmasternodecount", ""));

    UniValue obj(UniValue::VOBJ);
    int nCount = 0;
    int ipv4 = 0, ipv6 = 0, onion = 0;

    if (chainActive.Tip())
        mnodeman.GetNextMasternodeInQueueForPayment(chainActive.Tip()->nHeight, true, nCount);

    mnodeman.CountNetworks(MIN_PEER_PROTO_VERSION, ipv4, ipv6, onion);

    obj.pushKV("total", mnodeman.size());
    obj.pushKV("stable", mnodeman.stable_size());
    obj.pushKV("obfcompat", mnodeman.CountEnabled(MIN_PEER_PROTO_VERSION));
    obj.pushKV("enabled", mnodeman.CountEnabled());
    obj.pushKV("inqueue", nCount);
    obj.pushKV("ipv4", ipv4);
    obj.pushKV("ipv6", ipv6);
    obj.pushKV("onion", onion);

    return obj;
}

static UniValue broadcaststartmasternode(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2 || request.params.size() < 1)
        throw runtime_error(
            "broadcaststartmasternode hex sig\n"
            "\nVerifies the escrowed funds for the masternode and returns the necessary info for your and its configuration files.\n"

            "\nArguments:\n"
            "1. broadcast_hex			 (hex, required) hex representation of broadcast data.\n"
            "2. appendBroadcastSignature (hex, optional) hex representation of collateral signature.\n"
            "\nResult:\n"
            "\"status\"	(string) status of broadcast\n");

    const auto& params = request.params;
    UniValue result(UniValue::VOBJ);
    CMasternodeBroadcast mnb = readFromHex<CMasternodeBroadcast>(params[0].get_str());
    if(params.size()==2) 
    {
        mnb.sig = ParseHex(params[1].get_str());
    }

    int nDoS = 0;
    if(mnb.CheckAndUpdate(nDoS,*g_connman) && 
        mnb.CheckInputsAndAdd(nDoS,*g_connman))
    {
        mnb.Relay(*g_connman);
        result.push_back(Pair("status", "success"));
    }
    else
    {
        result.push_back(Pair("status","failed"));
    }
    return result;
}

static UniValue startmasternode(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> wallet = GetWalletForJSONRPCRequest(request);
    CWallet* pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 2 || request.params.size() == 0)
        throw runtime_error(
                "startmasternode alias\n"
                "\nVerifies the escrowed funds for the masternode and returns the necessary info for your and its configuration files.\n"

                "\nArguments:\n"
                "1. alias			(string, required) helpful identifier to recognize this allocation later.\n"
                "2. deferRelay      (bool, optional) returns broadcast data to delegate signaling masternode start.\n"
                "\nResult:\n"
                "\"status\"	(string) status of masternode\n");

    auto alias = request.params[0].get_str();
    bool deferRelay = (request.params.size()==2)? request.params[1].get_bool(): false;

    UniValue result(UniValue::VOBJ);
    bool fFound = false;
    for(auto &&configEntry : masternodeConfig.getEntries())
    {
        if(configEntry.getAlias() == alias)
        {
            fFound = true;
            std::string strError;
            CMasternodeBroadcast mnb;
            if(CActiveMasternode::Register(
                *pwallet, 
                configEntry, 
                strError,
                *g_connman,
                deferRelay,
                mnb))
            {
                result.pushKV("status", "success");
                if(deferRelay)
                {
                    CDataStream ss(SER_NETWORK,PROTOCOL_VERSION);
                    ss << mnb;
                    result.pushKV("broadcast_data", HexStr(ss.str()) );   
                }
            }
            else
            {
                result.pushKV("status", "failed");
                result.pushKV("error", strError);
            }

            break;
        }
    }

    if(!fFound)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid alias, couldn't find MN. Check your masternode.conf file");

    return result;
}

static UniValue getmasternodestatus (const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 0))
        throw runtime_error(
                "getmasternodestatus\n"
                "\nPrint masternode status\n"

                "\nResult:\n"
                "{\n"
                "  \"txhash\": \"xxxx\",      (string) Collateral transaction hash\n"
                "  \"outputidx\": n,        (numeric) Collateral transaction output index number\n"
                "  \"netaddr\": \"xxxx\",     (string) Masternode network address\n"
                "  \"addr\": \"xxxx\",        (string) PIVX address for masternode payments\n"
                "  \"status\": \"xxxx\",      (string) Masternode status\n"
                "  \"message\": \"xxxx\"      (string) Masternode status message\n"
                "}\n"

                "\nExamples:\n" +
                HelpExampleCli("getmasternodestatus", "") + HelpExampleRpc("getmasternodestatus", ""));

    if (!fMasterNode) throw runtime_error("This is not a masternode");

    CMasternode* pmn = mnodeman.Find(activeMasternode.vin);

    if (pmn) {
        UniValue mnObj(UniValue::VOBJ);
        mnObj.pushKV("txhash", activeMasternode.vin.prevout.hash.ToString());
        mnObj.pushKV("outputidx", (uint64_t)activeMasternode.vin.prevout.n);
        mnObj.pushKV("netaddr", activeMasternode.service.ToString());
        mnObj.pushKV("addr", EncodeDestination(pmn->pubKeyCollateralAddress.GetID()));
        mnObj.pushKV("status", activeMasternode.status);
        mnObj.pushKV("message", activeMasternode.GetStatus());
        return mnObj;
    }
    throw runtime_error("Masternode not found in the list of available masternodes. Current status: "
                        + activeMasternode.GetStatus());
}

UniValue mnsync(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw runtime_error(
                "mnsync [status|reset]\n"
                "Returns the sync status, updates to the next step or resets it entirely.\n"
                );

    std::string strMode = request.params[0].get_str();

    if(strMode == "status") {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("IsBlockchainSynced", masternodeSync.IsBlockchainSynced());
        obj.pushKV("lastMasternodeList", masternodeSync.lastMasternodeList);
        obj.pushKV("lastMasternodeWinner", masternodeSync.lastMasternodeWinner);
        obj.pushKV("lastFailure", masternodeSync.lastFailure);
        obj.pushKV("nCountFailures", masternodeSync.nCountFailures);
        obj.pushKV("sumMasternodeList", masternodeSync.sumMasternodeList);
        obj.pushKV("sumMasternodeWinner", masternodeSync.sumMasternodeWinner);
        obj.pushKV("countMasternodeList", masternodeSync.countMasternodeList);
        obj.pushKV("countMasternodeWinner", masternodeSync.countMasternodeWinner);
        obj.pushKV("RequestedMasternodeAssets", masternodeSync.RequestedMasternodeAssets);
        obj.pushKV("RequestedMasternodeAttempt", masternodeSync.RequestedMasternodeAttempt);
        return obj;
    }

    if(strMode == "reset")
    {
        masternodeSync.Reset();
        return "success";
    }

    if(strMode == "next")
    {
        if(masternodeSync.RequestedMasternodeAssets != MASTERNODE_SYNC_FINISHED)
        {
            masternodeSync.GetNextAsset();
        }
        return "success";
    }

    return "failure";
}

// clang-format off
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
  { "masternode",            "listmasternodes",          &listmasternodes,          {"filter"} },
  { "masternode",            "mnsync",                   &mnsync,                   {"command"} },
  { "masternode",            "getmasternodestatus",      &getmasternodestatus,      {}},
  { "masternode",            "startmasternode",          &startmasternode,          {"alias"}},
  { "masternode",            "getmasternodecount",       &getmasternodecount,       {}},
  { "masternode",            "masternodeconnect",        &masternodeconnect,        {"address"}},
  { "masternode",            "listmasternodes",          &listmasternodes,          {"filter"}},
  { "masternode",            "fundmasternode",           &fundmasternode,           {"alias", "amount", "txid", "type"}},
  { "masternode",            "allocatefunds",            &allocatefunds,            {"purpose", "identifier", "amount"}},
  { "masternode",            "setupmasternode",          &setupmasternode,            {"alias","txin","outputidx","collateralPubKey","ipaddress"}},
  { "masternode",            "broadcaststartmasternode",            &broadcaststartmasternode,            {"hex", "sig"}},
};
// clang-format on

void RegisterMasternodeRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
