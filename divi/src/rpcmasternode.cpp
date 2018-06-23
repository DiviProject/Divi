// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "db.h"
#include "init.h"
#include "main.h"
#include "masternode-budget.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "rpcserver.h"
#include "utilmoneystr.h"
#include "script/standard.h"

#include <boost/tokenizer.hpp>

#include <fstream>
using namespace json_spirit;

Value debug(const Array& params, bool fHelp)
{
	Object obj;

	// obj.push_back(Pair("pubKey", pubkey.GetHash().GetHex()));

	return obj;
}

Value allocatefunds(const Array& params, bool fHelp)
{
	if (fHelp || params.size() != 3)
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

	if (params[0].get_str() != "masternode") throw runtime_error("Surely you meant the first argument to be ""masternode"" . . . . ");
	CBitcoinAddress acctAddr = GetAccountAddress("alloc->" + params[1].get_str());
	string strAmt = params[2].get_str();
	CAmount nAmount;
	if (strAmt == "diamond") { nAmount = Diamond.collateral; }
	else if (strAmt == "platinum") { nAmount = Platinum.collateral; }
	else if (strAmt == "gold") { nAmount = Gold.collateral; }
	else if (strAmt == "silver") { nAmount = Silver.collateral; }
	else if (strAmt == "copper") { nAmount = Copper.collateral; }
	// else if (strAmt.find_first_not_of( "0123456789" ) == string::npos) nAmount = AmountFromValue(params[2]);
	else throw runtime_error("invalid amount");

	CWalletTx wtx;
	SendMoney(acctAddr.Get(), nAmount, wtx);

	Object obj;
	obj.push_back(Pair("txhash", wtx.GetHash().GetHex()));
	return obj;
}

Value fundmasternode(const Array& params, bool fHelp)
{
	if (fHelp || params.size() < 3 || params.size() > 6)
		throw runtime_error(
			"fundmasternode alias amount TxID masternode ( \"pay wallet\" ( \"voting wallet\" ) )\n"
			"\nVerifies the escrowed funds for the masternode and returns the necessary info for your and its configuration files.\n"

			"\nArguments:\n"
			"1. alias			(string, required) helpful identifier to recognize this allocation later.\n"
			"2. amount			(diamond, platinum, gold, silver, copper) tier of masternode. \n"
			"      <future>     (numeric, required) amount of divi funded will also be accepted for partially funding master nodes and other purposes.\n"
			"3. TxID			(string, required) funding transaction id .\n"
			"4. masternode		(string, required) address of masternode.\n"
			"5. pay wallet		(string, optional) public key of pay wallet (if different from funding wallet).\n"
			"6. voting wallet	(string, optional) public key of voting wallet (if different from pay wallet).\n"
			"(use an empty string for the pay wallet if the same as the funding wallet and you wish to assign a different voting wallet).\n"

			"\nResult:\n"
			"\"config line\"	(string) the above details for the masternode & wallet config files & cryptographic signature proving that you authorized this.\n");

	string strAmt = params[1].get_str();
	CAmount nAmount;
	if (strAmt == "diamond") { nAmount = Diamond.collateral; }
	else if (strAmt == "platinum") { nAmount = Platinum.collateral; }
	else if (strAmt == "gold") { nAmount = Gold.collateral; }
	else if (strAmt == "silver") { nAmount = Silver.collateral; }
	else if (strAmt == "copper") { nAmount = Copper.collateral; }
	// else if (strAmt.find_first_not_of( "0123456789" ) == string::npos) nAmount = AmountFromValue(params[2]);
	else throw runtime_error("invalid amount");

	uint256 txHash = uint256(params[2].get_str());
	std::string mnAddress = params[3].get_str();

	// Temporary unlock MN coins from masternode.conf
	vector<COutPoint> confLockedCoins;
	if (GetBoolArg("-mnconflock", true)) {
		uint256 mnTxHash;
		BOOST_FOREACH(CMasternodeEntry mne, masternodeConfig.entries) {
			mnTxHash.SetHex(mne.txHash);

			int nIndex;
			try {
				nIndex = std::stoi(mne.outputIndex);
			}
			catch (const std::exception e) {
				LogPrintf("%s: %s on getOutputIndex\n", __func__, e.what());
				continue;
			}

			COutPoint outpoint = COutPoint(mnTxHash, nIndex);
			confLockedCoins.push_back(outpoint);
			pwalletMain->UnlockCoin(outpoint);
		}
	}

	vector<COutput> vCoins;
	pwalletMain->AvailableCoins(vCoins);
	const COutput* selectedOutput;
	bool found = false;
	for (vector<COutput>::iterator it = vCoins.begin(); it != vCoins.end(); it++) {
		if ((*it).tx->GetHash() == txHash && (*it).tx->vout[(*it).i].nValue == nAmount) {
			selectedOutput = &(*it);
			found = true;
			break;
		}
	}

	if (!found) throw JSONRPCError(RPC_VERIFY_ERROR, "Couldn't verify transaction");

	CMnFunding funds = CMnFunding({ nAmount, CTxIn(txHash, selectedOutput->i) });
	if (!funds.CheckVin(mnAddress)) throw JSONRPCError(RPC_VERIFY_ERROR, "Transaction is not valid");

	// Lock MN coins from masternode.conf back if they where temporary unlocked
	if (!confLockedCoins.empty()) {
		BOOST_FOREACH(COutPoint outpoint, confLockedCoins)
			pwalletMain->LockCoin(outpoint);
	}

	std::string pWallet, vWallet;
	if (params.size() < 5) pWallet = GetAccountAddress("", false).ToString(); else pWallet = params[4].get_str();
	if (params.size() < 6) vWallet = pWallet; else vWallet = params[5].get_str();

	string config = params[1].get_str() + " " + params[2].get_str() + " " + to_string(selectedOutput->i) + " " + mnAddress + " " + pWallet + " " + vWallet;

	vector<unsigned char> vchSig;
	CTxDestination address1;
	ExtractDestination(selectedOutput->tx->vout[selectedOutput->i].scriptPubKey, address1);
	CBitcoinAddress address2(address1);
	mnodeman.my->SignMsg(address2.ToString(), config, vchSig);

	config = params[0].get_str() + " " + config + " " + EncodeBase64(&vchSig[0], vchSig.size());

	Object obj;
	obj.push_back(Pair("config line", config));
	return obj;
}

Value getpoolinfo(const Array& params, bool fHelp)
{
	throw runtime_error("Obfuscation, in general, and getpoolinfo, in particular, are deprecated!");
}

Value masternode(const Array& params, bool fHelp)
{
	throw runtime_error("masternode is deprecated!  Use one of the newer, clearer commands.");
}

string nodeHelp(string indent = "")
{
	string ret = indent + "\"address\": \"address\",    (string) Masternode DIVI address\n";
	ret += indent + "\"protocol\": xxxx,        (numeric) Protocol version\n";
	ret += indent + "\"netaddr\": \"xxxx\",       (string) Masternode network address\n";;
	ret += indent + "\"lastseen\": ttt,			(numeric) The time in seconds since last seen\n";
	ret += indent + "\"activetime\": ttt,		(numeric) The time in seconds masternode has been active\n";
	ret += indent + "\"lastpaid\": ttt,			(numeric) The time in seconds since masternode was last paid\n";
	return ret;
}

void nodeStatus(Object* obj, CMasternode* mn)
{
	obj->push_back(Pair("address", mn->address));
	obj->push_back(Pair("tier", mn->tier.name));
	obj->push_back(Pair("protocol", mn->protocolVersion));
	obj->push_back(Pair("netaddr", mn->service.ToString()));
	obj->push_back(Pair("lastseen", GetAdjustedTime() - mn->lastPing.sigTime));
	obj->push_back(Pair("activetime", mn->lastPing.sigTime - mn->sigTime));
	// obj->push_back(Pair("lastpaid", GetAdjustedTime() - mn->lastPaid));
}

Value listmasternodes(const Array& params, bool fHelp)
{
	std::string strFilter = "";

	if (params.size() == 1) strFilter = params[0].get_str();

	if (fHelp || (params.size() > 1))
		throw runtime_error(
			"listmasternodes ( \"filter\" )\n"
			"\nGet a ranked list of masternodes\n"

			"\nArguments:\n"
			"1. \"filter\"    (string, optional) Filter search text. Partial match by Divi address or ip address.\n"

			"\nResult:\n"
			"[\n"
			"  {\n"
			"    \"rank\": n,				(numeric) Masternode Rank (or 0 if not enabled)\n" +
			nodeHelp("    ") +
			"  }\n"
			"  ,...\n"
			"]\n"
			"\nExamples:\n" +
			HelpExampleCli("masternodelist", "") + HelpExampleRpc("masternodelist", ""));

	Object ret;
	int i = 1;
	//if (mnodeman.vCurrScores.size() == 0) {
	if (mnodeman.mMasternodes.size() > 0) {
		ret.push_back(Pair("Warning:", "Masternode selection process currently in process so list is *unranked*"));
		ret.push_back(Pair("mnodeman.mMasternodes.size:", to_string(mnodeman.mMasternodes.size())));
		for (map<uint256, CMasternode>::iterator it = mnodeman.mMasternodes.begin(); it != mnodeman.mMasternodes.end(); it++, i++) {
			Object obj;
			CMasternode mn = (*it).second;
			if (strFilter != "" && mn.address.find(strFilter) == string::npos && mn.service.ToString().find(strFilter) == string::npos) continue;
			nodeStatus(&obj, &mn);
			ret.push_back(Pair("Result:", obj));
		}
	}
	/*else for (vector<pair<uint256, string>>::iterator it = mnodeman.vCurrScores.begin(); it != mnodeman.vCurrScores.end(); it++, i++) {
		Object obj;
		CMasternode* mn = mnodeman.Find((*it).second);
		if (strFilter != "" && mn->address.find(strFilter) == string::npos && mn->service.ToString().find(strFilter) == string::npos) continue;
		obj.push_back(Pair("rank", i));
		nodeStatus(&obj, mn);
		ret.push_back(Pair("Result:", obj));
	}*/
	return ret;
}

Value masternodeconnect(const Array& params, bool fHelp)
{
	if (fHelp || (params.size() != 1))
		throw runtime_error(
			"masternodeconnect \"address\"\n"
			"\nAttempts to connect to specified masternode address\n"

			"\nArguments:\n"
			"1. \"address\"     (string, required) IP or net address to connect to\n"

			"\nExamples:\n" +
			HelpExampleCli("masternodeconnect", "\"192.168.0.6:51472\"") + HelpExampleRpc("masternodeconnect", "\"192.168.0.6:51472\""));

	std::string strAddress = params[0].get_str();

	CService addr = CService(strAddress);

	CNode* pnode = ConnectNode((CAddress)addr, NULL, false);
	if (pnode) {
		pnode->Release();
		return Value::null;
	}
	else {
		throw runtime_error("error connecting\n");
	}
}

Value getmasternodecount(const Array& params, bool fHelp)
{
	if (fHelp || (params.size() > 0))
		throw runtime_error(
			"getmasternodecount\n"
			"\nGet masternode count values\n"

			"\nResult:\n"
			"{\n"
			"  \"total\": n,        (numeric) Total masternodes\n"
			"  \"stable\": n,       (numeric) Stable count\n"
			"}\n"
			"\nExamples:\n" +
			HelpExampleCli("getmasternodecount", "") + HelpExampleRpc("getmasternodecount", ""));

	Object obj;
	obj.push_back(Pair("total", to_string(mnodeman.mMasternodes.size())));
	obj.push_back(Pair("stable", mnodeman.stableSize));
	return obj;
}

Value masternodecurrent(const Array& params, bool fHelp)
{
	if (fHelp || (params.size() != 0))
		throw runtime_error(
			"masternodecurrent\n"
			"\nGet current masternode winner\n"

			"\nResult:\n"
			"{\n" +
			nodeHelp("  ") +
			"}\n"
			"\nExamples:\n" +
			HelpExampleCli("masternodecurrent", "") + HelpExampleRpc("masternodecurrent", ""));

	Object obj;
	if (mnodeman.vCurrScores[0].size() == 0) { obj.push_back(Pair("Warning:", "Current masternode selection still in process")); return obj; }
	nodeStatus(&obj, mnodeman.Find(mnodeman.vCurrScores[0][0].second));
	return obj;
}

Value startmasternode(const Array& params, bool fHelp)
{
	throw runtime_error("startmasternode is deprecated!  Master nodes now automatically start themselves");
}

Value createmasternodekey(const Array& params, bool fHelp)
{
	throw runtime_error("createmasternodekey is deprecated!  A masternodekey is no longer necessary for setup.");
}

Value getmasternodeoutputs(const Array& params, bool fHelp)
{
	throw runtime_error("getmasternodeoutputs is deprecated!  It has been replaced by fundmasternode for setup.");
}

Value listmasternodeconf(const Array& params, bool fHelp)
{
	throw runtime_error("listmasternodeconf is deprecated!  It is not necessary for setup.");
}

Value getmasternodestatus(const Array& params, bool fHelp)
{
	if (fHelp || (params.size() != 0))
		throw runtime_error(
			"getmasternodestatus\n"
			"\nPrint masternode status\n"

			"\nResult:\n"
			"{\n" +
			nodeHelp("  ") +
			"}\n"
			"\nExamples:\n" +
			HelpExampleCli("getmasternodestatus", "") + HelpExampleRpc("getmasternodestatus", ""));

	if (!fMasterNode) throw runtime_error("This is not a masternode");

	Object obj;
	nodeStatus(&obj, mnodeman.Find(mnodeman.my->address));
	return obj;
}

Value getmasternodewinners(const Array& params, bool fHelp)
{
	throw runtime_error("getmasternodewinners is a work in progress!  Check back soon.");
	/*
	if (fHelp || params.size() > 3)
		throw runtime_error(
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
	{
		LOCK(cs_main);
		CBlockIndex* pindex = chainActive.Tip();
		if (!pindex) return 0;
		nHeight = pindex->nHeight;
	}

	int nLast = 10;
	std::string strFilter = "";

	if (params.size() >= 1)
		nLast = atoi(params[0].get_str());

	if (params.size() == 2)
		strFilter = params[1].get_str();

	Array ret;

	for (int i = nHeight - nLast; i < nHeight + 20; i++) {
		Object obj;
		obj.push_back(Pair("nHeight", i));

		std::string strPayment = GetRequiredPaymentsString(i);
		if (strFilter != "" && strPayment.find(strFilter) == std::string::npos) continue;

		if (strPayment.find(',') != std::string::npos) {
			Array winner;
			boost::char_separator<char> sep(",");
			boost::tokenizer< boost::char_separator<char> > tokens(strPayment, sep);
			BOOST_FOREACH(const string& t, tokens) {
				Object addr;
				std::size_t pos = t.find(":");
				std::string strAddress = t.substr(0, pos);
				uint64_t nVotes = atoi(t.substr(pos + 1));
				addr.push_back(Pair("address", strAddress));
				addr.push_back(Pair("nVotes", nVotes));
				winner.push_back(addr);
			}
			obj.push_back(Pair("winner", winner));
		}
		else if (strPayment.find("Unknown") == std::string::npos) {
			Object winner;
			std::size_t pos = strPayment.find(":");
			std::string strAddress = strPayment.substr(0, pos);
			uint64_t nVotes = atoi(strPayment.substr(pos + 1));
			winner.push_back(Pair("address", strAddress));
			winner.push_back(Pair("nVotes", nVotes));
			obj.push_back(Pair("winner", winner));
		}
		else {
			Object winner;
			winner.push_back(Pair("address", strPayment));
			winner.push_back(Pair("nVotes", 0));
			obj.push_back(Pair("winner", winner));
		}

		ret.push_back(obj);
	}

	return ret;
	*/
	return 0;
}

Value getmasternodescores(const Array& params, bool fHelp)
{
	throw runtime_error("getmasternodewinners is a work in progress!  Check back soon.");
	/*
	if (fHelp || params.size() > 1)
		throw runtime_error(
			"getmasternodescores ( blocks )\n"
			"\nPrint list of winning masternode by score\n"

			"\nArguments:\n"
			"1. blocks      (numeric, optional) Show the last n blocks (default 10)\n"

			"\nResult:\n"
			"{\n"
			"  xxxx: \"xxxx\"   (numeric : string) Block height : Masternode hash\n"
			"  ,...\n"
			"}\n"
			"\nExamples:\n" +
			HelpExampleCli("getmasternodescores", "") + HelpExampleRpc("getmasternodescores", ""));

	int nLast = 10;

	if (params.size() == 1) {
		try {
			nLast = std::stoi(params[0].get_str());
		}
		catch (const boost::bad_lexical_cast &) {
			throw runtime_error("Exception on param 2");
		}
	}
	Object obj;

	std::vector<CMasternode> vMasternodes = mnodeman.GetFullMasternodeVector();
	for (int nHeight = chainActive.Tip()->nHeight - nLast; nHeight < chainActive.Tip()->nHeight + 20; nHeight++) {
		uint256 nHigh = 0;
		CMasternode* pBestMasternode = NULL;
		BOOST_FOREACH(CMasternode& mn, vMasternodes) {
			uint256 n = mn.CalculateScore(1, nHeight - 100);
			if (n > nHigh) {
				nHigh = n;
				pBestMasternode = &mn;
			}
		}
		if (pBestMasternode)
			obj.push_back(Pair(strprintf("%d", nHeight), pBestMasternode->vin.prevout.hash.ToString().c_str()));
	}

	return obj;
	*/
	return 0;
}
