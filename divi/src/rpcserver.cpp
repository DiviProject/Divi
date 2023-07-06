// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcserver.h"

#include <Logging.h>
#include <rpcprotocol.h>
#include "base58.h"
#include "init.h"
#include "ui_interface.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet.h"
#endif
#include "Settings.h"
#include <utilmoneystr.h>
#include <random.h>
#include <alert.h>
#include <Warnings.h>

#include <deque>

#include "json/json_spirit_writer_template.h"
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>

#include <AcceptedConnection.h>
#include <rest.h>

extern CCriticalSection cs_main;

// RPC Endpoints
extern json_spirit::Value getconnectioncount(const json_spirit::Array& params, bool fHelp, CWallet* pwallet); // in rpcnet.cpp
extern json_spirit::Value getpeerinfo(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value ping(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value addnode(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getaddednodeinfo(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getnettotals(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getnetworkinfo(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);

extern json_spirit::Value importprivkey(const json_spirit::Array& params, bool fHelp, CWallet* pwallet); // in rpcdump.cpp
extern json_spirit::Value importaddress(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value dumpprivkey(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value dumphdinfo(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value bip38paperwallet(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value bip38decrypt(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);

extern json_spirit::Value setgenerate(const json_spirit::Array& params, bool fHelp, CWallet* pwallet); // in rpcmining.cpp
extern json_spirit::Value generateblock(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getmininginfo(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value prioritisetransaction(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);

extern json_spirit::Value getnewaddress(const json_spirit::Array& params, bool fHelp, CWallet* pwallet); // in rpcwallet.cpp
extern json_spirit::Value getaccountaddress(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getrawchangeaddress(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value setaccount(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getaccount(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getaddressesbyaccount(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getcoinavailability(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value fundvault(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value reclaimvaultfunds(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value debitvaultbyname(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value removevault(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value addvault(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value sendtoaddress(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value signmessage(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getreceivedbyaddress(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getreceivedbyaccount(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getbalance(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getunconfirmedbalance(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value sendfrom(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value sendmany(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value addmultisigaddress(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value listreceivedbyaddress(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value listreceivedbyaccount(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value listtransactions(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value listaccounts(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value listsinceblock(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value gettransaction(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value backupwallet(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value keypoolrefill(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value walletpassphrase(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value walletpassphrasechange(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value walletlock(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value encryptwallet(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value lockunspent(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value listlockunspent(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getwalletinfo(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value loadwallet(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);

extern json_spirit::Value getrawtransaction(const json_spirit::Array& params, bool fHelp, CWallet* pwallet); // in rcprawtransaction.cpp
extern json_spirit::Value listunspent(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value createrawtransaction(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value decoderawtransaction(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value decodescript(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value signrawtransaction(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value sendrawtransaction(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value signtransactionwithaddresskey(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);

extern json_spirit::Value getlotteryblockwinners(const json_spirit::Array& params, bool fHelp, CWallet* pwallet); // in rpclottery.cpp

extern json_spirit::Value getblockcount(const json_spirit::Array& params, bool fHelp, CWallet* pwallet); // in rpcblockchain.cpp
extern json_spirit::Value getbestblockhash(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getdifficulty(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getrawmempool(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getblockhash(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getblock(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getblockheader(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value gettxoutsetinfo(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value gettxout(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value verifychain(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getblockchaininfo(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getchaintips(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getmempoolinfo(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value reverseblocktransactions(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value invalidateblock(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value reconsiderblock(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getinvalid(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);

extern json_spirit::Value allocatefunds(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);// in rpcmasternode.cpp
extern json_spirit::Value verifymasternodesetup(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value signmnbroadcast(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value setupmasternode(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value listmasternodes(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getmasternodecount(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value broadcaststartmasternode(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value startmasternode(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getmasternodestatus(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getmasternodewinners(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value importmnbroadcast(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value listmnbroadcasts(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);

extern json_spirit::Value ban(const json_spirit::Array& params, bool fHelp, CWallet* pwallet); // in rpcmisc.cpp
extern json_spirit::Value getinfo(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value mnsync(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value spork(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value validateaddress(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value createmultisig(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value verifymessage(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value setmocktime(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getaddresstxids(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getaddressdeltas(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getaddressbalance(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getspentinfo(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getaddressutxos(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value clearbanned(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value listbanned(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);
extern json_spirit::Value getstakingstatus(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);


using namespace boost;
using namespace boost::asio;
using namespace json_spirit;
using namespace std;
extern Settings& settings;
static std::string strRPCUserColonPass;

static bool fRPCRunning = false;
static bool fRPCInWarmup = true;
static std::string rpcWarmupStatus("RPC server started");
static CCriticalSection cs_rpcWarmup;

//! These are created by StartRPCThreads, destroyed in StopRPCThreads
static asio::io_service* rpc_io_service = NULL;
static map<string, boost::shared_ptr<deadline_timer> > deadlineTimers;
static boost::thread_group* rpc_worker_group = NULL;
static boost::asio::io_service::work* rpc_dummy_work = NULL;
static std::vector<CSubNet> rpc_allow_subnets; //!< List of subnets to allow RPC connections from
static std::vector<boost::shared_ptr<ip::tcp::acceptor> > rpc_acceptors;

std::string GetWarningMessage(std::string category)
{
    int alertPriorityValue = 0;
    std::pair<std::string,std::string> warningMessages = Warnings::GetWarnings(settings.GetBoolArg("-testsafemode",false),alertPriorityValue);
    CAlert::GetHighestPriorityWarning(alertPriorityValue, warningMessages.first);
    if (category == "statusbar")
        return translate(warningMessages.first.c_str());
    else if (category == "rpc")
        return translate(warningMessages.second.c_str());

    return translate("Error");
}

void RPCTypeCheck(const Array& params,
    const list<Value_type>& typesExpected,
    bool fAllowNull)
{
    unsigned int i = 0;
    BOOST_FOREACH (Value_type t, typesExpected) {
        if (params.size() <= i)
            break;

        const Value& v = params[i];
        if (!((v.type() == t) || (fAllowNull && (v.type() == null_type)))) {
            string err = strprintf("Expected type %s, got %s",
                Value_type_name[t], Value_type_name[v.type()]);
            throw JSONRPCError(RPC_TYPE_ERROR, err);
        }
        i++;
    }
}

void RPCTypeCheck(const Object& o,
    const map<string, Value_type>& typesExpected,
    bool fAllowNull)
{
    BOOST_FOREACH (const PAIRTYPE(string, Value_type) & t, typesExpected) {
        const Value& v = find_value(o, t.first);
        if (!fAllowNull && v.type() == null_type)
            throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Missing %s", t.first));

        if (!((v.type() == t.second) || (fAllowNull && (v.type() == null_type)))) {
            string err = strprintf("Expected type %s for %s, got %s",
                Value_type_name[t.second], t.first, Value_type_name[v.type()]);
            throw JSONRPCError(RPC_TYPE_ERROR, err);
        }
    }
}

/**
 * Note: This interface may still be subject to change.
 */

std::string CRPCTable::help(std::string strCommand, CWallet* pwallet) const
{
    std::string strRet;
    std::string category;
    std::set<rpcfn_type> setDone;
    std::vector<std::pair<std::string, const CRPCCommand*> > vCommands;

    for (std::map<std::string, const CRPCCommand*>::const_iterator mi = mapCommands.begin(); mi != mapCommands.end(); ++mi)
        vCommands.push_back(std::make_pair(mi->second->category + mi->first, mi->second));
    sort(vCommands.begin(), vCommands.end());

    BOOST_FOREACH (const PAIRTYPE(std::string, const CRPCCommand*) & command, vCommands) {
        const CRPCCommand* pcmd = command.second;
        std::string strMethod = pcmd->name;
        // We already filter duplicates, but these deprecated screw up the sort order
        if (strMethod.find("label") != string::npos)
            continue;
        if ((strCommand != "" || pcmd->category == "hidden") && strMethod != strCommand)
            continue;
#ifdef ENABLE_WALLET
        if (pcmd->requiresWalletLock && !pwallet)
            continue;
#endif

        try {
            Array params;
            rpcfn_type pfn = pcmd->actor;
            if (setDone.insert(pfn).second)
                (*pfn)(params, true, pwallet);
        } catch (std::exception& e) {
            // Help text is returned in an exception
            string strHelp = string(e.what());
            if (strCommand == "") {
                if (strHelp.find('\n') != string::npos)
                    strHelp = strHelp.substr(0, strHelp.find('\n'));

                if (category != pcmd->category) {
                    if (!category.empty())
                        strRet += "\n";
                    category = pcmd->category;
                    string firstLetter = category.substr(0, 1);
                    boost::to_upper(firstLetter);
                    strRet += "== " + firstLetter + category.substr(1) + " ==\n";
                }
            }
            strRet += strHelp + "\n";
        }
    }
    if (strRet == "")
        strRet = strprintf("help: unknown command: %s\n", strCommand);
    strRet = strRet.substr(0, strRet.size() - 1);
    return strRet;
}

Value help(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "help ( \"command\" )\n"
            "\nList all commands, or get help for a specified command.\n"
            "\nArguments:\n"
            "1. \"command\"     (string, optional) The command to get help on\n"
            "\nResult:\n"
            "\"text\"     (string) The help text\n");

    string strCommand;
    if (params.size() > 0)
        strCommand = params[0].get_str();

    return CRPCTable::getRPCTable().help(strCommand, pwallet);
}


Value stop(const Array& params, bool fHelp, CWallet* pwallet)
{
    // Accept the deprecated and ignored 'detach' boolean argument
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "stop\n"
            "\nStop DIVI server.");
    // Shutdown will take long enough that the response should get back
    StartShutdown();
    return "DIVI server stopping";
}


/**
 * Call Table
 */
static const CRPCCommand vRPCCommands[] =
    {
        //  category              name                      actor (function)         okSafeMode threadSafe requiresWalletLock requiresWalletInstance
        //  --------------------- ------------------------  -----------------------  ---------- ---------- ------------------ ----------------------
        /* Overall control/query calls */
        {"control", "getinfo", &getinfo, true, false, false,false}, /* uses wallet if enabled */
        {"control", "help", &help, true, true, false,false},
        {"control", "stop", &stop, true, true, false,false},

        /* P2P networking */
        {"network", "getnetworkinfo", &getnetworkinfo, true, false, false,false},
        {"network", "addnode", &addnode, true, true, false,false},
        {"network", "getaddednodeinfo", &getaddednodeinfo, true, true, false,false},
        {"network", "getconnectioncount", &getconnectioncount, true, false, false, false},
        {"network", "getnettotals", &getnettotals, true, true, false, false},
        {"network", "getpeerinfo", &getpeerinfo, true, false, false, false},
        {"network", "ping", &ping, true, false, false, false},

        /* Block chain and UTXO */
        {"blockchain", "getblockchaininfo", &getblockchaininfo, true, false, false, false},
        {"blockchain", "getbestblockhash", &getbestblockhash, true, false, false, false},
        {"blockchain", "getblockcount", &getblockcount, true, false, false, false},
        {"blockchain", "getlotteryblockwinners", &getlotteryblockwinners, true, false, false, false},
        {"blockchain", "getblock", &getblock, true, false, false, false},
        {"blockchain", "getblockhash", &getblockhash, true, false, false, false},
        {"blockchain", "getblockheader", &getblockheader, false, false, false, false},
        {"blockchain", "getchaintips", &getchaintips, true, false, false, false},
        {"blockchain", "getdifficulty", &getdifficulty, true, false, false, false},
        {"blockchain", "getmempoolinfo", &getmempoolinfo, true, true, false, false},
        {"blockchain", "getrawmempool", &getrawmempool, true, false, false, false},
        {"blockchain", "gettxout", &gettxout, true, false, false, false},
        {"blockchain", "gettxoutsetinfo", &gettxoutsetinfo, true, false, false, false},
        {"blockchain", "verifychain", &verifychain, true, false, false, false},
        {"blockchain", "reverseblocktransactions", &reverseblocktransactions, true, false, false, false},
        {"blockchain", "invalidateblock", &invalidateblock, true, false, false, false},
        {"blockchain", "reconsiderblock", &reconsiderblock, true, false, false, false},
        {"getinvalid", "getinvalid", &getinvalid, true, true, false, false},

        /* Mining */
        {"mining", "getmininginfo", &getmininginfo, true, false, false, false},
        {"mining", "prioritisetransaction", &prioritisetransaction, true, false, false, false},

#ifdef ENABLE_WALLET
        /* Coin generation */
        {"generating", "setgenerate", &setgenerate, true, true, false, true},
        {"generating", "generateblock", &generateblock, true, true, false, true},
#endif

        /* Raw transactions */
        {"rawtransactions", "createrawtransaction", &createrawtransaction, true, false, false, false},
        {"rawtransactions", "decoderawtransaction", &decoderawtransaction, true, false, false, false},
        {"rawtransactions", "decodescript", &decodescript, true, false, false, false},
        {"rawtransactions", "getrawtransaction", &getrawtransaction, true, false, false, false},
        {"rawtransactions", "sendrawtransaction", &sendrawtransaction, false, false, false, false},
        {"rawtransactions", "signtransactionwithaddresskey", &signtransactionwithaddresskey, false, false, false, true},
        {"rawtransactions", "signrawtransaction", &signrawtransaction, false, false, false, false}, /* uses wallet if enabled */

        /* Utility functions */
        {"util", "createmultisig", &createmultisig, true, true, false, false},
        {"util", "validateaddress", &validateaddress, true, false, false, false}, /* uses wallet if enabled */
        {"util", "verifymessage", &verifymessage, true, false, false, false},

        /* Not shown in help */
        {"hidden", "invalidateblock", &invalidateblock, true, false, false, false},
        {"hidden", "reconsiderblock", &reconsiderblock, true, false, false, false},
        {"hidden", "setmocktime", &setmocktime, true, false, false, false},

        /* Divi features */
		{ "divi", "allocatefunds", &allocatefunds, true, true, false, true},
		{"divi", "listmasternodes", &listmasternodes, true, true, false, false},
        {"divi", "getmasternodecount", &getmasternodecount, true, true, false,false},
        {"divi","setupmasternode",&setupmasternode,true,false,true,true},
        {"divi","signmnbroadcast",&signmnbroadcast,true,false,true,true},
        {"divi","verifymasternodesetup",&verifymasternodesetup,true,true,true, false},
        {"divi", "broadcaststartmasternode", &broadcaststartmasternode, true, true, false, false},
        {"divi", "startmasternode", &startmasternode, true, true, false, true},
        {"divi", "getmasternodestatus", &getmasternodestatus, true, true, false, false},
        {"divi", "getmasternodewinners", &getmasternodewinners, true, true, false, false},
        {"divi", "importmnbroadcast", &importmnbroadcast, true, false, false, false},
        {"divi", "listmnbroadcasts", &listmnbroadcasts, true, false, false, false},

        {"divi", "mnsync", &mnsync, true, true, false, false},
        {"divi", "spork", &spork, true, true, false, false},
        {"divi","ban",&ban,false,false,false, false},
        {"divi","clearbanned",&clearbanned,false,false,false, false},
        {"divi","listbanned",&listbanned,false,false,false, false},

        /* address index */
        { "addressindex", "getaddresstxids", &getaddresstxids, false, false, false, false },
        { "addressindex", "getaddressdeltas", &getaddressdeltas, false, false, false, false },
        { "addressindex", "getaddressbalance", &getaddressbalance, false, false, false, false },
        { "addressindex", "getaddressutxos", &getaddressutxos, false, false, false, false },

        { "blockchain", "getspentinfo", &getspentinfo, false, false, false, false },

#ifdef ENABLE_WALLET
        {"wallet", "addmultisigaddress", &addmultisigaddress, true, false, true, true},
        {"wallet", "backupwallet", &backupwallet, true, false, true, true},
        {"wallet", "dumpprivkey", &dumpprivkey, true, false, true, true},
        {"wallet", "dumphdinfo", &dumphdinfo, true, false, true, true},
        {"wallet", "bip38paperwallet", &bip38paperwallet, true, false, true, true},
        {"wallet", "bip38decrypt", &bip38decrypt, true, false, true, true},
        {"wallet", "encryptwallet", &encryptwallet, true, false, true, true},
        {"wallet", "getaccountaddress", &getaccountaddress, true, false, true, true},
        {"wallet", "getaccount", &getaccount, true, false, true, true},
        {"wallet", "getaddressesbyaccount", &getaddressesbyaccount, true, false, true, true},
        {"wallet", "getbalance", &getbalance, false, false, true, true},
        {"wallet", "getnewaddress", &getnewaddress, true, false, true, true},
        {"wallet", "getrawchangeaddress", &getrawchangeaddress, true, false, true, true},
        {"wallet", "getreceivedbyaccount", &getreceivedbyaccount, false, false, true, true},
        {"wallet", "getreceivedbyaddress", &getreceivedbyaddress, false, false, true, true},
        {"wallet", "getstakingstatus", &getstakingstatus, false, false, true, true},
        {"wallet", "gettransaction", &gettransaction, false, false, true, true},
        {"wallet", "getunconfirmedbalance", &getunconfirmedbalance, false, false, true, true},
        {"wallet", "getwalletinfo", &getwalletinfo, false, false, true, true},
        {"wallet", "loadwallet", &loadwallet, true, false, true, true},
        {"wallet", "importprivkey", &importprivkey, true, false, true, true},
        {"wallet", "importaddress", &importaddress, true, false, true, true},
        {"wallet", "keypoolrefill", &keypoolrefill, true, false, true, true},
        {"wallet", "listaccounts", &listaccounts, false, false, true, true},
        {"wallet", "listlockunspent", &listlockunspent, false, false, true, true},
        {"wallet", "listreceivedbyaccount", &listreceivedbyaccount, false, false, true, true},
        {"wallet", "listreceivedbyaddress", &listreceivedbyaddress, false, false, true, true},
        {"wallet", "listsinceblock", &listsinceblock, false, false, true, true},
        {"wallet", "listtransactions", &listtransactions, false, false, true, true},
        {"wallet", "listunspent", &listunspent, false, false, true, true},
        {"wallet", "lockunspent", &lockunspent, true, false, true, true},
        {"wallet", "sendfrom", &sendfrom, false, false, true, true},
        {"wallet", "sendmany", &sendmany, false, false, true, true},
        {"wallet", "sendtoaddress", &sendtoaddress, false, false, true, true},
        {"wallet", "fundvault", &fundvault, false, false, true, true},
        {"wallet", "reclaimvaultfunds", &reclaimvaultfunds, false, false, true, true},
        {"wallet", "debitvaultbyname", &debitvaultbyname, false, false, true, true},
        {"wallet", "removevault", &removevault, false, false, true, true},
        {"wallet", "addvault", &addvault, false, false, true, true},
        {"wallet", "getcoinavailability", &getcoinavailability, false, false, true, true},
        {"wallet", "setaccount", &setaccount, true, false, true, true},
        {"wallet", "signmessage", &signmessage, true, false, true, true},
        {"wallet", "walletlock", &walletlock, true, false, true, true},
        {"wallet", "walletpassphrasechange", &walletpassphrasechange, true, false, true, true},
        {"wallet", "walletpassphrase", &walletpassphrase, true, false, true, true},

#endif // ENABLE_WALLET
};

CRPCTable::CRPCTable()
{
    unsigned int vcidx;
    for (vcidx = 0; vcidx < (sizeof(vRPCCommands) / sizeof(vRPCCommands[0])); vcidx++) {
        const CRPCCommand* pcmd;

        pcmd = &vRPCCommands[vcidx];
        mapCommands[pcmd->name] = pcmd;
    }
}

const CRPCCommand* CRPCTable::operator[](string name) const
{
    map<string, const CRPCCommand*>::const_iterator it = mapCommands.find(name);
    if (it == mapCommands.end())
        return NULL;
    return (*it).second;
}


bool HTTPAuthorized(map<string, string>& mapHeaders)
{
    string strAuth = mapHeaders["authorization"];
    if (strAuth.substr(0, 6) != "Basic ")
        return false;
    string strUserPass64 = strAuth.substr(6);
    boost::trim(strUserPass64);
    string strUserPass = DecodeBase64(strUserPass64);
    return TimingResistantEqual(strUserPass, strRPCUserColonPass);
}

void ErrorReply(std::ostream& stream, const Object& objError, const Value& id)
{
    // Send error reply from json-rpc error object
    int nStatus = HTTP_INTERNAL_SERVER_ERROR;
    int code = find_value(objError, "code").get_int();
    if (code == RPC_INVALID_REQUEST)
        nStatus = HTTP_BAD_REQUEST;
    else if (code == RPC_METHOD_NOT_FOUND)
        nStatus = HTTP_NOT_FOUND;
    string strReply = JSONRPCReply(Value::null, objError, id);
    stream << HTTPReply(nStatus, strReply, false) << std::flush;
}

CNetAddr BoostAsioToCNetAddr(boost::asio::ip::address address)
{
    CNetAddr netaddr;
    // Make sure that IPv4-compatible and IPv4-mapped IPv6 addresses are treated as IPv4 addresses
    if (address.is_v6() && (address.to_v6().is_v4_compatible() || address.to_v6().is_v4_mapped()))
        address = address.to_v6().to_v4();

    if (address.is_v4()) {
        boost::asio::ip::address_v4::bytes_type bytes = address.to_v4().to_bytes();
        netaddr.SetRaw(NET_IPV4, &bytes[0]);
    } else {
        boost::asio::ip::address_v6::bytes_type bytes = address.to_v6().to_bytes();
        netaddr.SetRaw(NET_IPV6, &bytes[0]);
    }
    return netaddr;
}

bool ClientAllowed(const boost::asio::ip::address& address)
{
    CNetAddr netaddr = BoostAsioToCNetAddr(address);
    BOOST_FOREACH (const CSubNet& subnet, rpc_allow_subnets)
        if (subnet.Match(netaddr))
            return true;
    return false;
}

template <typename Protocol>
class AcceptedConnectionImpl : public AcceptedConnection
{
public:
    AcceptedConnectionImpl()
    {
    }

    virtual std::iostream& stream() override
    {
        return socketStream;
    }

    virtual std::string peer_address_to_string() const override
    {
        return peer.address().to_string();
    }

    virtual void close() override
    {
        socketStream.close();
    }

    typename Protocol::endpoint peer;
    typename Protocol::iostream socketStream;
};

void ServiceConnection(AcceptedConnection* conn);

//! Forward declaration required for RPCListen
template <typename Protocol>
static void RPCAcceptHandler(boost::shared_ptr<basic_socket_acceptor<Protocol> > acceptor,
    boost::shared_ptr<AcceptedConnection> conn,
    const boost::system::error_code& error);

/**
 * Sets up I/O resources to accept and handle a new connection.
 */
template <typename Protocol>
static void RPCListen(boost::shared_ptr<basic_socket_acceptor<Protocol> > acceptor)
{
    // Accept connection
    boost::shared_ptr<AcceptedConnectionImpl<Protocol> > conn(new AcceptedConnectionImpl<Protocol>());

    acceptor->async_accept(
        *conn->socketStream.rdbuf(),
        conn->peer,
        boost::bind(&RPCAcceptHandler<Protocol>,
            acceptor,
            conn,
            _1));
}


/**
 * Accept and handle incoming connection.
 */
template <typename Protocol>
static void RPCAcceptHandler(boost::shared_ptr<basic_socket_acceptor<Protocol> > acceptor,
    boost::shared_ptr<AcceptedConnection> conn,
    const boost::system::error_code& error)
{
    // Immediately start accepting new connections, except when we're cancelled or our socket is closed.
    if (error != asio::error::operation_aborted && acceptor->is_open())
        RPCListen(acceptor);

    AcceptedConnectionImpl<Protocol>* tcp_conn = dynamic_cast<AcceptedConnectionImpl<Protocol>*>(conn.get());

    if (error) {
        // TODO: Actually handle errors
        LogPrintf("%s: Error: %s\n", __func__, error.message());
    }
    // Restrict callers by IP.  It is important to
    // do this before starting client thread, to filter out
    // certain DoS and misbehaving clients.
    else if (tcp_conn && !ClientAllowed(tcp_conn->peer.address())) {
        conn->stream() << HTTPError(HTTP_FORBIDDEN, false) << std::flush;
        conn->close();
    } else {
        ServiceConnection(conn.get());
        conn->close();
    }
}

static ip::tcp::endpoint ParseEndpoint(const std::string& strEndpoint, int defaultPort)
{
    std::string addr;
    int port = defaultPort;
    SplitHostPort(strEndpoint, port, addr);
    return ip::tcp::endpoint(asio::ip::address::from_string(addr), port);
}

void StartRPCThreads()
{
    rpc_allow_subnets.clear();
    rpc_allow_subnets.push_back(CSubNet("127.0.0.0/8")); // always allow IPv4 local subnet
    rpc_allow_subnets.push_back(CSubNet("::1"));         // always allow IPv6 localhost
    if (settings.ParameterIsSetForMultiArgs("-rpcallowip")) {
        const vector<string>& vAllow = settings.GetMultiParameter("-rpcallowip");
        BOOST_FOREACH (string strAllow, vAllow) {
            CSubNet subnet(strAllow);
            if (!subnet.IsValid()) {
                uiInterface.ThreadSafeMessageBox(
                    strprintf("Invalid -rpcallowip subnet specification: %s. Valid are a single IP (e.g. 1.2.3.4), a network/netmask (e.g. 1.2.3.4/255.255.255.0) or a network/CIDR (e.g. 1.2.3.4/24).", strAllow),
                    "", CClientUIInterface::MSG_ERROR);
                StartShutdown();
                return;
            }
            rpc_allow_subnets.push_back(subnet);
        }
    }
    std::string strAllowed;
    BOOST_FOREACH (const CSubNet& subnet, rpc_allow_subnets)
        strAllowed += subnet.ToString() + " ";
    LogPrint("rpc", "Allowing RPC connections from: %s\n", strAllowed);

    strRPCUserColonPass = settings.GetParameter("-rpcuser") + ":" + settings.GetParameter("-rpcpassword");
    if (((settings.GetParameter("-rpcpassword") == "") ||
            (settings.GetParameter("-rpcuser") == settings.GetParameter("-rpcpassword"))) &&
        Params().RequireRPCPassword()) {
        unsigned char rand_pwd[32];
        GetRandBytes(rand_pwd, 32);
        uiInterface.ThreadSafeMessageBox(strprintf(
                                             translate("To use divid, or the -server option to divi-qt, you must set an rpcpassword in the configuration file:\n"
                                               "%s\n"
                                               "It is recommended you use the following random password:\n"
                                               "rpcuser=divirpc\n"
                                               "rpcpassword=%s\n"
                                               "(you do not need to remember this password)\n"
                                               "The username and password MUST NOT be the same.\n"
                                               "If the file does not exist, create it with owner-readable-only file permissions.\n"
                                               "It is also recommended to set alertnotify so you are notified of problems;\n"
                                               "for example: alertnotify=echo %%s | mail -s \"DIVI Alert\" admin@foo.com\n"),
                                             settings.GetConfigFile().string(),
                                             EncodeBase58(&rand_pwd[0], &rand_pwd[0] + 32)),
            "", CClientUIInterface::MSG_ERROR | CClientUIInterface::SECURE);
        StartShutdown();
        return;
    }

    assert(rpc_io_service == NULL);
    rpc_io_service = new asio::io_service();

    std::vector<ip::tcp::endpoint> vEndpoints;
    bool bBindAny = false;
    int defaultPort = settings.GetArg("-rpcport", BaseParams().RPCPort());
    if (!settings.ParameterIsSet("-rpcallowip")) // Default to loopback if not allowing external IPs
    {
        vEndpoints.push_back(ip::tcp::endpoint(asio::ip::address_v6::loopback(), defaultPort));
        vEndpoints.push_back(ip::tcp::endpoint(asio::ip::address_v4::loopback(), defaultPort));
        if (settings.ParameterIsSet("-rpcbind")) {
            LogPrintf("WARNING: option -rpcbind was ignored because -rpcallowip was not specified, refusing to allow everyone to connect\n");
        }
    } else if (settings.ParameterIsSet("-rpcbind")) // Specific bind address
    {
        BOOST_FOREACH (const std::string& addr, settings.GetMultiParameter("-rpcbind")) {
            try {
                vEndpoints.push_back(ParseEndpoint(addr, defaultPort));
            } catch (const boost::system::system_error&) {
                uiInterface.ThreadSafeMessageBox(
                    strprintf(translate("Could not parse -rpcbind value %s as network address"), addr),
                    "", CClientUIInterface::MSG_ERROR);
                StartShutdown();
                return;
            }
        }
    } else { // No specific bind address specified, bind to any
        vEndpoints.push_back(ip::tcp::endpoint(asio::ip::address_v6::any(), defaultPort));
        vEndpoints.push_back(ip::tcp::endpoint(asio::ip::address_v4::any(), defaultPort));
        // Prefer making the socket dual IPv6/IPv4 instead of binding
        // to both addresses seperately.
        bBindAny = true;
    }

    bool fListening = false;
    std::string strerr;
    std::string straddress;
    BOOST_FOREACH (const ip::tcp::endpoint& endpoint, vEndpoints) {
        try {
            asio::ip::address bindAddress = endpoint.address();
            straddress = bindAddress.to_string();
            LogPrintf("Binding RPC on address %s port %i (IPv4+IPv6 bind any: %i)\n", straddress, endpoint.port(), bBindAny);
            boost::system::error_code v6_only_error;
            boost::shared_ptr<basic_socket_acceptor<ip::tcp> > acceptor(new basic_socket_acceptor<ip::tcp>(*rpc_io_service));

            acceptor->open(endpoint.protocol());
            acceptor->set_option(basic_socket_acceptor<ip::tcp>::reuse_address(true));

            // Try making the socket dual IPv6/IPv4 when listening on the IPv6 "any" address
            acceptor->set_option(boost::asio::ip::v6_only(
                                     !bBindAny || bindAddress != asio::ip::address_v6::any()),
                v6_only_error);

            acceptor->bind(endpoint);
            acceptor->listen(socket_base::max_connections);

            RPCListen(acceptor);

            rpc_acceptors.push_back(acceptor);
            fListening = true;
            rpc_acceptors.push_back(acceptor);
            // If dual IPv6/IPv4 bind successful, skip binding to IPv4 separately
            if (bBindAny && bindAddress == asio::ip::address_v6::any() && !v6_only_error)
                break;
        } catch (boost::system::system_error& e) {
            LogPrintf("ERROR: Binding RPC on address %s port %i failed: %s\n", straddress, endpoint.port(), e.what());
            strerr = strprintf(translate("An error occurred while setting up the RPC address %s port %u for listening: %s"), straddress, endpoint.port(), e.what());
        }
    }

    if (!fListening) {
        uiInterface.ThreadSafeMessageBox(strerr, "", CClientUIInterface::MSG_ERROR);
        StartShutdown();
        return;
    }

    rpc_worker_group = new boost::thread_group();
    for (int i = 0; i < settings.GetArg("-rpcthreads", 4); i++)
        rpc_worker_group->create_thread(boost::bind(&asio::io_service::run, rpc_io_service));
    fRPCRunning = true;
}

void StartDummyRPCThread()
{
    if (rpc_io_service == NULL) {
        rpc_io_service = new asio::io_service();
        /* Create dummy "work" to keep the thread from exiting when no timeouts active,
         * see http://www.boost.org/doc/libs/1_51_0/doc/html/boost_asio/reference/io_service.html#boost_asio.reference.io_service.stopping_the_io_service_from_running_out_of_work */
        rpc_dummy_work = new asio::io_service::work(*rpc_io_service);
        rpc_worker_group = new boost::thread_group();
        rpc_worker_group->create_thread(boost::bind(&asio::io_service::run, rpc_io_service));
        fRPCRunning = true;
    }
}

void StopRPCThreads()
{
    if (rpc_io_service == NULL) return;
    // Set this to false first, so that longpolling loops will exit when woken up
    fRPCRunning = false;

    // First, cancel all timers and acceptors
    // This is not done automatically by ->stop(), and in some cases the destructor of
    // asio::io_service can hang if this is skipped.
    boost::system::error_code ec;
    BOOST_FOREACH (const boost::shared_ptr<ip::tcp::acceptor>& acceptor, rpc_acceptors) {
        acceptor->cancel(ec);
        if (ec)
            LogPrintf("%s: Warning: %s when cancelling acceptor", __func__, ec.message());
    }
    rpc_acceptors.clear();
    BOOST_FOREACH (const PAIRTYPE(std::string, boost::shared_ptr<deadline_timer>) & timer, deadlineTimers) {
        timer.second->cancel(ec);
        if (ec)
            LogPrintf("%s: Warning: %s when cancelling timer", __func__, ec.message());
    }
    deadlineTimers.clear();

    rpc_io_service->stop();
    if (rpc_worker_group != NULL)
        rpc_worker_group->join_all();
    delete rpc_dummy_work;
    rpc_dummy_work = NULL;
    delete rpc_worker_group;
    rpc_worker_group = NULL;
    delete rpc_io_service;
    rpc_io_service = NULL;
}

bool IsRPCRunning()
{
    return fRPCRunning;
}

void SetRPCWarmupStatus(const std::string& newStatus)
{
    LOCK(cs_rpcWarmup);
    rpcWarmupStatus = newStatus;
}

void SetRPCWarmupFinished()
{
    LOCK(cs_rpcWarmup);
    assert(fRPCInWarmup);
    fRPCInWarmup = false;
}

bool RPCIsInWarmup(std::string* outStatus)
{
    LOCK(cs_rpcWarmup);
    if (outStatus)
        *outStatus = rpcWarmupStatus;
    return fRPCInWarmup;
}

void RPCRunHandler(const boost::system::error_code& err, boost::function<void(void)> func)
{
    if (!err)
        func();
}

/**
 * Run func nSeconds from now. Uses boost deadline timers.
 * Overrides previous timer <name> (if any).
 */
void RPCRunLater(const std::string& name, boost::function<void(void)> func, int64_t nSeconds)
{
    assert(rpc_io_service != NULL);

    if (deadlineTimers.count(name) == 0) {
        deadlineTimers.insert(std::make_pair(name,
            boost::shared_ptr<deadline_timer>(new deadline_timer(*rpc_io_service))));
    }
    deadlineTimers[name]->expires_from_now(posix_time::seconds(nSeconds));
    deadlineTimers[name]->async_wait(boost::bind(RPCRunHandler, _1, func));
}
void RPCDiscardRunLater(const string &name)
{
    auto it = deadlineTimers.find(name);

    if(it != std::end(deadlineTimers)) {
        it->second->cancel();
        deadlineTimers.erase(it);
    }
}

class JSONRequest
{
public:
    Value id;
    string strMethod;
    Array params;

    JSONRequest() { id = Value::null; }
    void parse(const Value& valRequest);
};

void JSONRequest::parse(const Value& valRequest)
{
    // Parse request
    if (valRequest.type() != obj_type)
        throw JSONRPCError(RPC_INVALID_REQUEST, "Invalid Request object");
    const Object& request = valRequest.get_obj();

    // Parse id now so errors from here on will have the id
    id = find_value(request, "id");

    // Parse method
    Value valMethod = find_value(request, "method");
    if (valMethod.type() == null_type)
        throw JSONRPCError(RPC_INVALID_REQUEST, "Missing method");
    if (valMethod.type() != str_type)
        throw JSONRPCError(RPC_INVALID_REQUEST, "Method must be a string");
    strMethod = valMethod.get_str();
    if (strMethod != "getblocktemplate")
        LogPrint("rpc", "ThreadRPCServer method=%s\n", SanitizeString(strMethod));

    // Parse params
    Value valParams = find_value(request, "params");
    if (valParams.type() == array_type)
        params = valParams.get_array();
    else if (valParams.type() == null_type)
        params = Array();
    else
        throw JSONRPCError(RPC_INVALID_REQUEST, "Params must be an array");
}


static Object JSONRPCExecOne(const Value& req)
{
    Object rpc_result;

    JSONRequest jreq;
    try {
        jreq.parse(req);

        Value result = CRPCTable::getRPCTable().execute(jreq.strMethod, jreq.params);
        rpc_result = JSONRPCReplyObj(result, Value::null, jreq.id);
    } catch (Object& objError) {
        rpc_result = JSONRPCReplyObj(Value::null, objError, jreq.id);
    } catch (std::exception& e) {
        rpc_result = JSONRPCReplyObj(Value::null,
            JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id);
    }

    return rpc_result;
}

static string JSONRPCExecBatch(const Array& vReq)
{
    Array ret;
    for (unsigned int reqIdx = 0; reqIdx < vReq.size(); reqIdx++)
        ret.push_back(JSONRPCExecOne(vReq[reqIdx]));

    return write_string(Value(ret), false) + "\n";
}

static bool HTTPReq_JSONRPC(AcceptedConnection* conn,
    string& strRequest,
    map<string, string>& mapHeaders,
    bool fRun)
{
    // Check authorization
    if (mapHeaders.count("authorization") == 0) {
        conn->stream() << HTTPError(HTTP_UNAUTHORIZED, false) << std::flush;
        return false;
    }

    if (!HTTPAuthorized(mapHeaders)) {
        LogPrintf("ThreadRPCServer incorrect password attempt from %s\n", conn->peer_address_to_string());
        /* Deter brute-forcing
           If this results in a DoS the user really
           shouldn't have their RPC port exposed. */
        MilliSleep(250);

        conn->stream() << HTTPError(HTTP_UNAUTHORIZED, false) << std::flush;
        return false;
    }

    JSONRequest jreq;
    try {
        // Parse request
        Value valRequest;
        if (!read_string(strRequest, valRequest))
            throw JSONRPCError(RPC_PARSE_ERROR, "Parse error");

        // Return immediately if in warmup
        {
            LOCK(cs_rpcWarmup);
            if (fRPCInWarmup)
                throw JSONRPCError(RPC_IN_WARMUP, rpcWarmupStatus);
        }

        string strReply;

        // singleton request
        if (valRequest.type() == obj_type) {
            jreq.parse(valRequest);

            Value result = CRPCTable::getRPCTable().execute(jreq.strMethod, jreq.params);

            // Send reply
            strReply = JSONRPCReply(result, Value::null, jreq.id);

            // array of requests
        } else if (valRequest.type() == array_type)
            strReply = JSONRPCExecBatch(valRequest.get_array());
        else
            throw JSONRPCError(RPC_PARSE_ERROR, "Top-level object parse error");

        conn->stream() << HTTPReplyHeader(HTTP_OK, fRun, strReply.size()) << strReply << std::flush;
    } catch (Object& objError) {
        ErrorReply(conn->stream(), objError, jreq.id);
        return false;
    } catch (std::exception& e) {
        ErrorReply(conn->stream(), JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id);
        return false;
    }
    return true;
}

void ServiceConnection(AcceptedConnection* conn)
{
    bool fRun = true;
    while (fRun && !ShutdownRequested()) {
        int nProto = 0;
        map<string, string> mapHeaders;
        string strRequest, strMethod, strURI;

        // Read HTTP request line
        if (!ReadHTTPRequestLine(conn->stream(), nProto, strMethod, strURI))
            break;

        // Read HTTP message headers and body
        ReadHTTPMessage(conn->stream(), mapHeaders, strRequest, nProto, MAX_SIZE);

        // HTTP Keep-Alive is false; close connection immediately
        if ((mapHeaders["connection"] == "close") || (!settings.GetBoolArg("-rpckeepalive", true)))
            fRun = false;

        // Process via JSON-RPC API
        if (strURI == "/") {
            if (!HTTPReq_JSONRPC(conn, strRequest, mapHeaders, fRun))
                break;

            // Process via HTTP REST API
        } else if (strURI.substr(0, 6) == "/rest/" && settings.GetBoolArg("-rest", false)) {
            if (!HTTPReq_REST(&RPCIsInWarmup,conn, strURI, mapHeaders, fRun))
                break;

        } else {
            conn->stream() << HTTPError(HTTP_NOT_FOUND, false) << std::flush;
            break;
        }
    }
}

json_spirit::Value CRPCTable::execute(const std::string& strMethod, const json_spirit::Array& params) const
{
    // Find method
    const CRPCCommand* pcmd = CRPCTable::getRPCTable()[strMethod];
    if (!pcmd)
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found");

    CWallet* pwallet = GetWallet();
#ifdef ENABLE_WALLET
    assert(!pcmd->requiresWalletLock || (!pcmd->threadSafe && pcmd->requiresWalletInstance) );
    if ((pcmd->requiresWalletLock || pcmd->requiresWalletInstance) && !pwallet)
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method unavailable due to manually disabled wallet");
#endif

    // Observe safe mode
    string strWarning = GetWarningMessage("rpc");
    if (strWarning != "" && !settings.GetBoolArg("-disablesafemode", false) &&
        !pcmd->okSafeMode)
        throw JSONRPCError(RPC_FORBIDDEN_BY_SAFE_MODE, string("Safe mode: ") + strWarning);

    try {
        // Execute
        Value result;
        {
            if (pcmd->threadSafe)
            {
                result = pcmd->actor(params, false, pwallet);
            }
#ifdef ENABLE_WALLET
            else if (!pwallet)
            {
                assert(!(pcmd->requiresWalletLock || pcmd->requiresWalletInstance)); // Implied by above condition failing on wallet
                LOCK(cs_main);
                result = pcmd->actor(params, false, pwallet);
            } else {
                // Not threadsafe and may or may not require
                while (true) {
                    TRY_LOCK(cs_main, lockMain);
                    if (!lockMain) {
                        MilliSleep(50);
                        continue;
                    }
                    else if(!pcmd->requiresWalletLock)
                    {
                        result = pcmd->actor(params, false, pwallet);
                        break;
                    }
                    else
                    {
                        while (true)
                        {
                            TRY_LOCK(pwallet->getWalletCriticalSection(), lockWallet);
                            if (!lockMain) {
                                MilliSleep(50);
                                continue;
                            }
                            result = pcmd->actor(params, false, pwallet);
                            break;
                        }
                    }
                    break;
                }
            }
#else  // ENABLE_WALLET
            else {
                LOCK(cs_main);
                result = pcmd->actor(params, false, pwallet);
            }
#endif // !ENABLE_WALLET
        }
        return result;
    } catch (std::exception& e) {
        throw JSONRPCError(RPC_MISC_ERROR, e.what());
    }
}

std::vector<std::string> CRPCTable::listCommands() const
{
    std::vector<std::string> commandList;
    typedef std::map<std::string, const CRPCCommand*> commandMap;

    std::transform( mapCommands.begin(), mapCommands.end(),
                   std::back_inserter(commandList),
                   boost::bind(&commandMap::value_type::first,_1) );
    return commandList;
}

static int64_t nWalletUnlockTime;
void EnsureWalletIsUnlocked(CWallet* pwallet)
{
    if (pwallet && !pwallet->IsFullyUnlocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");
}

struct CancellableMethod
{
private:
    mutable bool executionComplete;
    mutable bool cancelled;
    bool unlockedForStakingOnExpiry;
    CWallet* walletRef_;

public:
    CancellableMethod(
        bool revertToUnlockedForStakingOnExpiry,
        CWallet* walletReference
        ): executionComplete(false)
        , cancelled(false)
        , unlockedForStakingOnExpiry(revertToUnlockedForStakingOnExpiry)
        , walletRef_(walletReference)
    {
    }

    void cancel()
    {
        if(walletRef_) AssertLockHeld(walletRef_->getWalletCriticalSection());
        cancelled = true;
    }

    bool canBeRemoved() const
    {
        if(walletRef_) AssertLockHeld(walletRef_->getWalletCriticalSection());
        return executionComplete;
    }
    void revertWalletUnlockStatus() const
    {
        if(walletRef_)
        {
            LOCK(walletRef_->getWalletCriticalSection()); // Required for modifying unlock time
            if(cancelled){ executionComplete = true; return; }
            RPCDiscardRunLater("lockwallet");
            nWalletUnlockTime = 0;
            if(unlockedForStakingOnExpiry) walletRef_->UnlockForStakingOnly();
            else walletRef_->LockFully();
            executionComplete = true;
            return;
        }
        executionComplete = true;
    }
};
static std::deque<CancellableMethod> cancellableCallStack;

void removeCompletedOrCancelPendingCalls()
{
    for(auto it = cancellableCallStack.begin(); it != cancellableCallStack.end(); )
    {
        if(it->canBeRemoved())
        {
            it = cancellableCallStack.erase(it);
            continue;
        }
        else
        {
            it->cancel();
            ++it;
        }
    }
}

enum class WalletLockMode
{
    UNLOCK,
    LOCK,
};
void ModifyWalletLockStateBriefly(CWallet* pwallet, WalletLockMode mode, int64_t sleepTime = 0, bool revertToUnlockedForStakingOnExpiry = false)
{
    if(pwallet)
    {
        AssertLockHeld(pwallet->getWalletCriticalSection());
        nWalletUnlockTime = GetTime() + sleepTime;
        removeCompletedOrCancelPendingCalls();
        if(mode==WalletLockMode::LOCK)
        {
            RPCDiscardRunLater("lockwallet");
            nWalletUnlockTime = 0;
            pwallet->LockFully();
        }
        else if (sleepTime > 0 && mode == WalletLockMode::UNLOCK)
        {
            nWalletUnlockTime = GetTime () + sleepTime;
            cancellableCallStack.emplace_back(revertToUnlockedForStakingOnExpiry,pwallet);
            RPCRunLater ("lockwallet", boost::bind (&CancellableMethod::revertWalletUnlockStatus, &cancellableCallStack.back()), sleepTime);
        }
    }
}

void LockWallet(CWallet* pwallet)
{
    ModifyWalletLockStateBriefly(pwallet, WalletLockMode::LOCK);
}
void UnlockWalletBriefly(CWallet* pwallet,int64_t sleepTime, bool revertToUnlockedForStakingOnExpiry)
{
    ModifyWalletLockStateBriefly(pwallet,WalletLockMode::UNLOCK,sleepTime,revertToUnlockedForStakingOnExpiry);
}

int64_t TimeTillWalletLock(CWallet* pwallet)
{
    AssertLockHeld(pwallet->getWalletCriticalSection());
    return nWalletUnlockTime;
}

std::string HelpRequiringPassphrase(CWallet* pwallet)
{
    return pwallet && pwallet->IsCrypted() ? "\nRequires wallet passphrase to be set with walletpassphrase call." : "";
}

std::string HelpExampleCli(string methodname, string args)
{
    return "> divi-cli " + methodname + " " + args + "\n";
}

std::string HelpExampleRpc(string methodname, string args)
{
    return "> curl --user myusername --data-binary '{\"jsonrpc\": \"1.0\", \"id\":\"curltest\", "
           "\"method\": \"" +
           methodname + "\", \"params\": [" + args + "] }' -H 'content-type: text/plain;' http://127.0.0.1:51473/\n";
}