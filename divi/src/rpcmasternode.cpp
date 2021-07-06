// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "db.h"
#include "init.h"
#include "main.h"
#include "masternode-payments.h"
#include "activemasternode.h"
#include "masternodeman.h"
#include "obfuscation.h"
#include "rpcserver.h"
#include "utilmoneystr.h"
#include "script/standard.h"
#include <base58.h>
#include <wallet.h>
#include <WalletTx.h>
#include <Logging.h>
#include <masternode-sync.h>
#include <MasternodeModule.h>
#include <MasternodeBroadcastFactory.h>
#include <MasternodeHelpers.h>
#include <version.h>
#include <RpcMasternodeFeatures.h>

#include <boost/tokenizer.hpp>
#include <boost/algorithm/string.hpp>
#include <fstream>
using namespace json_spirit;

extern CWallet* pwalletMain;
extern CCriticalSection cs_main;
extern void SendMoney(const CTxDestination& address, CAmount nValue, CWalletTx& wtxNew, bool spendFromVaults = false);
extern CBitcoinAddress GetAccountAddress(std::string strAccount, bool bForceNew = false);

static MasternodeTier GetMasternodeTierFromString(std::string str)
{
    boost::algorithm::to_lower(str); // modifies str

    if(str == "copper") {
        return MasternodeTier::COPPER;
    }
    else if(str == "silver") {
        return MasternodeTier::SILVER;
    }
    else if(str == "gold") {
        return MasternodeTier::GOLD;
    }
    else if(str == "platinum") {
        return MasternodeTier::PLATINUM;
    }
    else if(str == "diamond") {
        return MasternodeTier::DIAMOND;
    }

    return MasternodeTier::INVALID;
}

Value debug(const Array& params, bool fHelp)
{
	Object obj;

	// obj.push_back(Pair("pubKey", pubkey.GetHash().GetHex()));

	return obj;
}

Value allocatefunds(const Array& params, bool fHelp)
{
	if (fHelp || params.size() != 3)
		throw std::runtime_error(
			"allocatefunds purpose alias amount ( \"pay wallet\" ( \"voting wallet\" ) )\n"
			"\nStarts escrows funds for some purpose.\n"

			"\nArguments:\n"
			"1. purpose			(string, required) Helpful identifier to recognize this allocation later.  Currently only masternode is recognized. \n"
			"2. identifier      (string, required) Helpful Identifier to recognize the specific instance of the funding type that the funding is for. \n"
			"3. amount			(diamond, platinum, gold, silver, copper) tier of masternode. \n"
			"      <future>     (numeric, required) amount of divi funded will also be accepted for partially funding master nodes and other purposes.\n"

			"\nResult:\n"
			"\"vin\"			(string) funding transaction id or bare txid necessary for next step.\n");

    if (params[0].get_str() != "masternode")
    {
        throw std::runtime_error("Surely you meant the first argument to be ""masternode"" . . . . ");
    }
	CBitcoinAddress acctAddr = GetAccountAddress("alloc->" + params[1].get_str());
	std::string strAmt = params[2].get_str();

    auto nMasternodeTier = GetMasternodeTierFromString(strAmt);
    if(!CMasternode::IsTierValid(nMasternodeTier))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid masternode tier");
    }

    EnsureWalletIsUnlocked();

    CWalletTx wtx;
    SendMoney(acctAddr.Get(), CMasternode::GetTierCollateralAmount(nMasternodeTier), wtx);

    Object obj;
    obj.push_back(Pair("txhash", wtx.GetHash().GetHex()));
    return obj;
}

Value fundmasternode(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 4)
		throw std::runtime_error(
			"fundmasternode alias amount TxID masternode ( \"pay wallet\" ( \"voting wallet\" ) )\n"
			"\nVerifies the escrowed funds for the masternode and returns the necessary info for your and its configuration files.\n"

			"\nArguments:\n"
			"1. alias			(string, required) helpful identifier to recognize this allocation later.\n"
			"2. amount			(diamond, platinum, gold, silver, copper) tier of masternode. \n"
			"      <future>     (numeric, required) amount of divi funded will also be accepted for partially funding master nodes and other purposes.\n"
			"3. TxID			(string, required) funding transaction id or bare txid.\n"
            "4. masternode		(string, required) ip address of masternode.\n"
			"(use an empty string for the pay wallet if the same as the funding wallet and you wish to assign a different voting wallet).\n"

			"\nResult:\n"
			"\"config line\"	(string) the above details for the masternode & wallet config files & cryptographic signature proving that you authorized this.\n");

    auto alias = params[0].get_str();
    auto nMasternodeTier = GetMasternodeTierFromString(params[1].get_str());

    if(!CMasternode::IsTierValid(nMasternodeTier))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid masternode tier");
    }

	uint256 txHash = uint256(params[2].get_str());
	std::string mnAddress = params[3].get_str();

	bool found = false;
    auto nAmount = CMasternode::GetTierCollateralAmount(nMasternodeTier);
    bool outputIndex = -1;
    if(auto wtx = pwalletMain->GetWalletTx(txHash))
    {
        for(size_t i = 0; i < wtx->vout.size(); ++i)
        {
            if(wtx->vout[i].nValue == nAmount && !pwalletMain->IsSpent(*wtx, i))
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

    EnsureWalletIsUnlocked();
    pwalletMain->TopUpKeyPool();

    // Generate a new key that is added to wallet
    CBitcoinAddress address = GetAccountAddress("reserved->" + alias);

    CKeyID keyID;
    if (!address.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");

    CKey vchSecret;
    if (!pwalletMain->GetKey(keyID, vchSecret))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + CBitcoinAddress(keyID).ToString() + " is not known");

    if (mnAddress.find(':') == std::string::npos)
        mnAddress += ":" + std::to_string(Params().GetDefaultPort());

    auto tokens = {
        alias,
        mnAddress,
        CBitcoinSecret(vchSecret).ToString(),
        txHash.ToString(),
        std::to_string(outputIndex)
    };

	Object obj;
    obj.push_back(Pair("config line", boost::algorithm::join(tokens, " ")));
	return obj;
}
Value verifymasternodesetup(const Array&params, bool fHelp)
{
    if (fHelp || params.size() != 4)
		throw std::runtime_error(
			"verifymasternodesetup ip_address sigtime collateralPubKey masternodePubKey\n"
			"\nStarts escrows funds for some purpose.\n"

			"\nArguments:\n"
            "1. ip_address			 (string, required) Local ip address of this node. \n"
			"2. sigtime              (string, required) Timestamp for signature \n"
            "3. collateralPubKey     (string, required) Collateral pubkey \n"
            "4. masternodePubKey     (string, required) Masternode pubkey. \n"
			"\nResult:\n"
			"\"expected_message\"			    (bool) Expected masternode-broadcast message\n");

    Object result;
    try
    {
        std::string pubkeyString =params[2].get_str();
        std::string mnPubkeyString = params[3].get_str();

        CMasternodeBroadcast mnb;
        mnb.addr = CService(params[0].get_str()+":"+std::to_string(Params().GetDefaultPort()) );
        mnb.sigTime = static_cast<int64_t>(std::stoll(params[1].get_str()));
        mnb.pubKeyCollateralAddress = CPubKey(pubkeyString.begin(),pubkeyString.end());
        mnb.pubKeyMasternode = CPubKey(mnPubkeyString.begin(), mnPubkeyString.end());
        mnb.protocolVersion = PROTOCOL_VERSION;

        result.push_back(Pair("expected_message_to_sign",HexStr(mnb.getMessageToSign())));

        return result;
    }
    catch(...)
    {
        throw JSONRPCError(RPC_INVALID_PARAMS,std::string("Error parsing input arguments\n"));
    }
    return result;
}

Value setupmasternode(const Array& params, bool fHelp)
{
	if (fHelp || params.size() < 5 || params.size() > 6)
		throw std::runtime_error(
			"setupmasternode alias txhash outputIndex collateralPubKey ip_address\n"
			"\nStarts escrows funds for some purpose.\n"

			"\nArguments:\n"
			"1. alias			    (string, required) Helpful identifier to recognize this masternode later. \n"
			"2. txHash              (string, required) Funding transaction hash or bare txid. \n"
            "3. outputIndex         (string, required) Output index transaction. \n"
            "4. collateralPubkey    (string, required) collateral pubkey. \n"
            "5. ip_address          (string, required) Local ip address of this node\n"
			"\nResult:\n"
			"\"protocol_version\"			(string) Protocol version used for serialization.\n"
            "\"message_to_sign\"			(string) Hex-encoded msg requiring collateral signature.\n"
            "\"config_line\"			    (string) Configuration data needed in the.\n"
            "\"broadcast_data\"			    (string) funding transaction id necessary for next step.\n");

    Object result;

    EnsureWalletIsUnlocked();

    CBitcoinAddress address = GetAccountAddress("reserved->" + params[0].get_str() );
    CKeyID keyID;
    if (!address.GetKeyID(keyID))
    {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    }
    CKey masternodeKey;
    if (!pwalletMain->GetKey(keyID, masternodeKey))
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + CBitcoinAddress(keyID).ToString() + " is not known");
    }

    std::string alias = params[0].get_str();
    std::string txHash = params[1].get_str();
    std::string outputIndex = params[2].get_str();
    std::vector<unsigned char> pubkeyStr = ParseHex(static_cast<std::string>(params[3].get_str()));
    CPubKey pubkeyCollateralAddress;
    pubkeyCollateralAddress.Set(pubkeyStr.begin(),pubkeyStr.end());

    std::string ipAndPort = params[4].get_str();
    if (ipAndPort.find(':') == std::string::npos)
        ipAndPort += ":" + std::to_string(Params().GetDefaultPort());

    CMasternodeConfig::CMasternodeEntry config(alias,ipAndPort,CBitcoinSecret(masternodeKey).ToString(),txHash,outputIndex);

    CMasternodeBroadcast mnb;
    std::string errorMsg;
    if(!CMasternodeBroadcastFactory::Create(config,pubkeyCollateralAddress,errorMsg,mnb))
    {
        throw JSONRPCError(RPC_INVALID_PARAMS,errorMsg);
    }

    CDataStream ss(SER_NETWORK,PROTOCOL_VERSION);
    result.push_back(Pair("protocol_version", PROTOCOL_VERSION ));
    result.push_back(Pair("message_to_sign", HexStr(mnb.getMessageToSign()) ));
    result.push_back(Pair("config_line",
        config.getAlias()+" "+
        ipAndPort +" "+
        CBitcoinSecret(masternodeKey).ToString()+" "
        +txHash+" "+outputIndex));
    ss << mnb;
    result.push_back(Pair("broadcast_data", HexStr(ss.str()) ));
    return result;
}

Value getpoolinfo(const Array& params, bool fHelp)
{
	throw std::runtime_error("Obfuscation, in general, and getpoolinfo, in particular, are deprecated!");
}

Value masternode(const Array& params, bool fHelp)
{
	throw std::runtime_error("masternode is deprecated!  Use one of the newer, clearer commands.");
}

std::string nodeHelp(std::string indent = "")
{
	std::string ret = indent + "\"address\": \"address\",    (string) Masternode DIVI address\n";
	ret += indent + "\"protocol\": xxxx,        (numeric) Protocol version\n";
//	ret += indent + "\"netaddr\": \"xxxx\",       (string) Masternode network address\n";;
	ret += indent + "\"lastseen\": ttt,			(numeric) The time in seconds since last seen\n";
	ret += indent + "\"activetime\": ttt,		(numeric) The time in seconds masternode has been active\n";
	ret += indent + "\"lastpaid\": ttt,			(numeric) The time in seconds since masternode was last paid\n";
	return ret;
}

Value listmasternodes(const Array& params, bool fHelp)
{
    std::string strFilter = "";

    if (params.size() == 1) strFilter = params[0].get_str();

    if (fHelp || (params.size() > 1))
        throw std::runtime_error(
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
            "    \"addr\": \"addr\",      (string) Masternode DIVI address\n"
            "    \"version\": v,        (numeric) Masternode protocol version\n"
            "    \"lastseen\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last seen\n"
            "    \"activetime\": ttt,   (numeric) The time in seconds since epoch (Jan 1 1970 GMT) masternode has been active\n"
            "    \"lastpaid\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) masternode was last paid\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("masternodelist", "") + HelpExampleRpc("masternodelist", ""));

    Array ret;
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if(!pindex) return 0;
    }

    std::vector<MasternodeListEntry> masternodeList = GetMasternodeList(strFilter);
    ret.reserve(masternodeList.size());
    for(auto& masternodeEntry : masternodeList)
    {
        Object obj;
        obj.reserve(10);
        obj.emplace_back("network", masternodeEntry.network);
        obj.emplace_back("txhash", masternodeEntry.txHash);
        obj.emplace_back("outidx", masternodeEntry.outputIndex);
        obj.emplace_back("status", masternodeEntry.status);
        obj.emplace_back("addr", masternodeEntry.collateralAddress);
        obj.emplace_back("version", masternodeEntry.protocolVersion);
        obj.emplace_back("lastseen", masternodeEntry.lastSeenTime);
        obj.emplace_back("activetime", masternodeEntry.activeTime);
        obj.emplace_back("lastpaid", masternodeEntry.lastPaidTime);
        obj.emplace_back("tier",masternodeEntry.masternodeTier );

        ret.emplace_back(obj);
    }

    return ret;
}

Value getmasternodecount (const Array& params, bool fHelp)
{
    if (fHelp || (params.size() > 0))
        throw std::runtime_error(
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

    const CBlockIndex* tip = chainActive.Tip();
    MasternodeCountData data = GetMasternodeCounts(tip);

    Object obj;
    obj.push_back(Pair("total", data.total));
    obj.push_back(Pair("stable", data.stable));
    obj.push_back(Pair("obfcompat", data.enabledAndActive));
    obj.push_back(Pair("enabled", data.enabled));
    obj.push_back(Pair("inqueue", data.queueCount));
    obj.push_back(Pair("ipv4", data.ipv4));
    obj.push_back(Pair("ipv6", data.ipv6));
    obj.push_back(Pair("onion", data.onion));
    return obj;
}

Value masternodecurrent(const Array& params, bool fHelp)
{
	throw std::runtime_error("masternodecurrent is deprecated!  masternode payments always rely upon votes");
}

Value broadcaststartmasternode(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2 || params.size() < 1)
        throw std::runtime_error(
            "broadcaststartmasternode hex sig\n"
            "\nVerifies the escrowed funds for the masternode and returns the necessary info for your and its configuration files.\n"

            "\nArguments:\n"
            "1. broadcast_hex			 (hex, required) hex representation of broadcast data.\n"
            "2. appendBroadcastSignature (hex, optional) hex representation of collateral signature.\n"
            "\nResult:\n"
            "\"status\"	(string) status of broadcast\n");

    bool updatePing = false;
    std::string signature;
    if(params.size()==2)
    {
        if (params[1].get_str() != "update_ping")
            signature = params[1].get_str();
        updatePing = true;
    }

    const auto mnResult = RelayMasternodeBroadcast(params[0].get_str(), signature, updatePing);

    Object result;
    result.emplace_back("status", mnResult.status ? "success" : "failed");
    if(!mnResult.status)
        result.emplace_back("error", mnResult.errorMessage);

    return result;
}

Value startmasternode(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "startmasternode alias\n"
            "\nVerifies the escrowed funds for the masternode and returns the necessary info for your and its configuration files.\n"

            "\nArguments:\n"
            "1. alias			(string, required) helpful identifier to recognize this allocation later.\n"
            "2. deferRelay  (bool, optional) returns broadcast data to delegate signaling masternode start.\n"
            "\nResult:\n"
            "\"status\"	(string) status of masternode\n");

    const std::string alias = params[0].get_str();
    const bool deferRelay = (params.size() == 1)? false: params[1].get_bool();

    EnsureWalletIsUnlocked();
    Object result;
    MasternodeStartResult mnResult = StartMasternode(*pwalletMain,alias,deferRelay);

    result.push_back(Pair("status",mnResult.status?"success":"failed"));
    if(!mnResult.status)
    {
        result.push_back(Pair("error",mnResult.errorMessage));
        return result;
    }
    else if(deferRelay)
    {
        result.push_back(Pair("broadcastData",mnResult.broadcastData));
        return result;
    }
    return result;
}

Value createmasternodekey(const Array& params, bool fHelp)
{
	throw std::runtime_error("createmasternodekey is deprecated!  A masternodekey is no longer necessary for setup.");
}

Value getmasternodeoutputs(const Array& params, bool fHelp)
{
	throw std::runtime_error("getmasternodeoutputs is deprecated!  It has been replaced by fundmasternode for setup.");
}

Value listmasternodeconf(const Array& params, bool fHelp)
{
	throw std::runtime_error("listmasternodeconf is deprecated!  It is not necessary for setup.");
}

Value getmasternodestatus (const Array& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw std::runtime_error(
            "getmasternodestatus\n"
            "\nPrint masternode status\n"

            "\nResult:\n"
            "{\n"
            "  \"txhash\": \"xxxx\",      (string) Collateral transaction hash\n"
            "  \"outputidx\": n,        (numeric) Collateral transaction output index number\n"
            "  \"netaddr\": \"xxxx\",     (string) Masternode network address\n"
            "  \"addr\": \"xxxx\",        (string) DIVI address for masternode payments\n"
            "  \"status\": \"xxxx\",      (string) Masternode status\n"
            "  \"message\": \"xxxx\"      (string) Masternode status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodestatus", "") + HelpExampleRpc("getmasternodestatus", ""));

    const ActiveMasternodeStatus activeMNStatus = GetActiveMasternodeStatus();
    if(!activeMNStatus.activeMasternodeFound)
    {
        throw std::runtime_error("Masternode not found in the list of available masternodes. Current status: "
                    + activeMNStatus.statusMessage);
    }
    Object mnObj;
    mnObj.push_back(Pair("txhash", activeMNStatus.txHash ));
    mnObj.push_back(Pair("outputidx", (uint64_t)std::stoi(activeMNStatus.outputIndex) ));
    mnObj.push_back(Pair("netaddr", activeMNStatus.netAddress));
    mnObj.push_back(Pair("addr", activeMNStatus.collateralAddress));
    mnObj.push_back(Pair("status", std::stoi(activeMNStatus.statusCode) ));
    mnObj.push_back(Pair("message", activeMNStatus.statusMessage));
    return mnObj;
}

Value getmasternodewinners (const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw std::runtime_error(
            "getmasternodewinners ( blocks \"filter\" )\n"
            "\nPrint the masternode winners for the last n blocks\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional) Number of previous blocks to show (default: 10)\n"
            "2. filter      (string, optional) Search filter matching MN address\n"

            "\nResult (single winner):\n"
            "[\n"
            "  {\n"
            "    \"nHeight\": n,           (numeric) block height\n"
            "    \"winner\": {\n"
            "      \"address\": \"xxxx\",    (string) DIVI MN Address\n"
            "      \"nVotes\": n,          (numeric) Number of votes for winner\n"
            "    }\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nResult (multiple winners):\n"
            "[\n"
            "  {\n"
            "    \"nHeight\": n,           (numeric) block height\n"
            "    \"winner\": [\n"
            "      {\n"
            "        \"address\": \"xxxx\",  (string) DIVI MN Address\n"
            "        \"nVotes\": n,        (numeric) Number of votes for winner\n"
            "      }\n"
            "      ,...\n"
            "    ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getmasternodewinners", "") + HelpExampleRpc("getmasternodewinners", ""));

    int nHeight;
    CBlockIndex* pindex = nullptr;
    {
        LOCK(cs_main);
        pindex = chainActive.Tip();
        if(!pindex) return 0;
        nHeight = pindex->nHeight;
    }
    assert(pindex != nullptr);

    int nLast = 10;
    std::string strFilter = "";

    if (params.size() >= 1)
        nLast = atoi(params[0].get_str());

    if (params.size() == 2)
        strFilter = params[1].get_str();

    Array ret;
    const CMasternodePayments& mnpayments = GetMasternodeModule().getMasternodePayments();
    for (int i = nHeight - nLast; i < nHeight + 20; i++) {
        Object obj;
        obj.push_back(Pair("nHeight", i));

        uint256 seedHash;
        if (!GetBlockHashForScoring(seedHash, pindex, i - nHeight)) continue;
        std::string strPayment = mnpayments.GetRequiredPaymentsString(seedHash);
        if (strFilter != "" && strPayment.find(strFilter) == std::string::npos) continue;

        if (strPayment.find(',') != std::string::npos) {
            Array winner;
            boost::char_separator<char> sep(",");
            boost::tokenizer< boost::char_separator<char> > tokens(strPayment, sep);
            BOOST_FOREACH (const std::string& t, tokens) {
                Object addr;
                std::size_t pos = t.find(":");
                std::string strAddress = t.substr(0,pos);
                uint64_t nVotes = atoi(t.substr(pos+1));
                addr.push_back(Pair("address", strAddress));
                addr.push_back(Pair("nVotes", nVotes));
                winner.push_back(addr);
            }
            obj.push_back(Pair("winner", winner));
        } else if (strPayment.find("Unknown") == std::string::npos) {
            Object winner;
            std::size_t pos = strPayment.find(":");
            std::string strAddress = strPayment.substr(0,pos);
            uint64_t nVotes = atoi(strPayment.substr(pos+1));
            winner.push_back(Pair("address", strAddress));
            winner.push_back(Pair("nVotes", nVotes));
            obj.push_back(Pair("winner", winner));
        } else {
            Object winner;
            winner.push_back(Pair("address", strPayment));
            winner.push_back(Pair("nVotes", 0));
            obj.push_back(Pair("winner", winner));
        }

            ret.push_back(obj);
    }

    return ret;
}
Value getmasternodescores(const Array& params, bool fHelp)
{
	throw std::runtime_error("getmasternodescores is deprecated!  masternode payments always rely upon votes not current scores");
}
