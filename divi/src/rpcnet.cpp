// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpcprotocol.h>
#include "rpcserver.h"

#include "clientversion.h"
#include "main.h"
#include "net.h"
#include <Node.h>
#include "netbase.h"
#include "protocol.h"
#include "sync.h"
#include "timedata.h"
#include "utiltime.h"
#include "version.h"
#include "chainparams.h"
#include <FeeRate.h>
#include <FeeAndPriorityCalculator.h>
#include <NodeStats.h>
#include <QueuedBlock.h>
#include <NodeState.h>
#include <NodeStateRegistry.h>
#include <tinyformat.h>

#include <boost/foreach.hpp>

#include "json/json_spirit_value.h"

using namespace json_spirit;
using namespace std;

extern CCriticalSection cs_main;

Value getconnectioncount(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getconnectioncount\n"
            "\nReturns the number of connections to other nodes.\n"
            "\nbResult:\n"
            "n          (numeric) The connection count\n"
            "\nExamples:\n" +
            HelpExampleCli("getconnectioncount", "") + HelpExampleRpc("getconnectioncount", ""));

    return GetPeerCount();
}

Value ping(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "ping\n"
            "\nRequests that a ping be sent to all other nodes, to measure ping time.\n"
            "Results provided in getpeerinfo, pingtime and pingwait fields are decimal seconds.\n"
            "Ping command is handled in queue with all other commands, so it measures processing backlog, not just network ping.\n"
            "\nExamples:\n" +
            HelpExampleCli("ping", "") + HelpExampleRpc("ping", ""));

    // Request that each node send a ping during next message processing pass
    SchedulePingingPeers();
    return Value::null;
}

Value getpeerinfo(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getpeerinfo\n"
            "\nReturns data about each connected network node as a json array of objects.\n"
            "\nbResult:\n"
            "[\n"
            "  {\n"
            "    \"id\": n,                   (numeric) Peer index\n"
            "    \"addr\":\"host:port\",      (string) The ip address and port of the peer\n"
            "    \"addrlocal\":\"ip:port\",   (string) local address\n"
            "    \"services\":\"xxxxxxxxxxxxxxxx\",   (string) The services offered\n"
            "    \"lastsend\": ttt,           (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last send\n"
            "    \"lastrecv\": ttt,           (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last receive\n"
            "    \"bytessent\": n,            (numeric) The total bytes sent\n"
            "    \"bytesrecv\": n,            (numeric) The total bytes received\n"
            "    \"conntime\": ttt,           (numeric) The connection time in seconds since epoch (Jan 1 1970 GMT)\n"
            "    \"pingtime\": n,             (numeric) ping time\n"
            "    \"pingwait\": n,             (numeric) ping wait\n"
            "    \"version\": v,              (numeric) The peer version, such as 7001\n"
            "    \"subver\": \"/Divi Core:x.x.x.x/\",  (string) The string version\n"
            "    \"inbound\": true|false,     (boolean) Inbound (true) or Outbound (false)\n"
            "    \"startingheight\": n,       (numeric) The starting height (block) of the peer\n"
            "    \"banscore\": n,             (numeric) The ban score\n"
            "    \"synced_headers\": n,       (numeric) The last header we have in common with this peer\n"
            "    \"synced_blocks\": n,        (numeric) The last block we have in common with this peer\n"
            "    \"inflight\": [\n"
            "       n,                        (numeric) The heights of blocks we're currently asking from this peer\n"
            "       ...\n"
            "    ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getpeerinfo", "") + HelpExampleRpc("getpeerinfo", ""));

    std::vector<std::pair<CNodeStats,CNodeStateStats>> allStats;
    {
        LOCK(cs_main);
        GetNodeStateStats(allStats);
    }

    Array ret;
    for(const std::pair<CNodeStats,CNodeStateStats>& statsPair: allStats)
    {
        const CNodeStats& stats = statsPair.first;
        const CNodeStateStats& statestats = statsPair.second;
        Object obj;
        obj.push_back(Pair("id", stats.nodeid));
        obj.push_back(Pair("addr", stats.addrName));
        if (!(stats.addrLocal.empty()))
            obj.push_back(Pair("addrlocal", stats.addrLocal));
        obj.push_back(Pair("services", strprintf("%016x", stats.nServices)));
        obj.push_back(Pair("lastsend", stats.nLastSend));
        obj.push_back(Pair("lastrecv", stats.nLastRecv));
        obj.push_back(Pair("bytessent", stats.nSendBytes));
        obj.push_back(Pair("bytesrecv", stats.nRecvBytes));
        obj.push_back(Pair("conntime", stats.nTimeConnected));
        obj.push_back(Pair("pingtime", stats.dPingTime));
        if (stats.dPingWait > 0.0)
            obj.push_back(Pair("pingwait", stats.dPingWait));
        obj.push_back(Pair("version", stats.nVersion));
        // Use the sanitized form of subver here, to avoid tricksy remote peers from
        // corrupting or modifiying the JSON output by putting special characters in
        // their ver message.
        obj.push_back(Pair("subver", stats.cleanSubVer));
        obj.push_back(Pair("inbound", stats.fInbound));
        obj.push_back(Pair("startingheight", stats.nStartingHeight));
        if(statestats.stateFound)
        {
            obj.push_back(Pair("banscore", statestats.nMisbehavior));
            obj.push_back(Pair("synced_headers", statestats.nSyncHeight));
            obj.push_back(Pair("synced_blocks", statestats.nCommonHeight));
            Array heights;
            for(int height: statestats.vHeightInFlight)
            {
                heights.push_back(height);
            }
            obj.push_back(Pair("inflight", heights));
        }
        obj.push_back(Pair("whitelisted", stats.fWhitelisted));

        ret.push_back(obj);
    }

    return ret;
}

Value addnode(const Array& params, bool fHelp, CWallet* pwallet)
{
    string strCommand;
    if (params.size() == 2)
        strCommand = params[1].get_str();
    if (fHelp || params.size() != 2 ||
        (strCommand != "onetry" && strCommand != "add" && strCommand != "remove"))
        throw runtime_error(
            "addnode \"node\" \"add|remove|onetry\"\n"
            "\nAttempts add or remove a node from the addnode list.\n"
            "Or try a connection to a node once.\n"
            "\nArguments:\n"
            "1. \"node\"     (string, required) The node (see getpeerinfo for nodes)\n"
            "2. \"command\"  (string, required) 'add' to add a node to the list, 'remove' to remove a node from the list, 'onetry' to try a connection to the node once\n"
            "\nExamples:\n" +
            HelpExampleCli("addnode", "\"192.168.0.6:51472\" \"onetry\"") + HelpExampleRpc("addnode", "\"192.168.0.6:51472\", \"onetry\""));

    const string strNode = params[0].get_str();
    if(!addNode(strNode,strCommand))
    {
        if(strCommand == "add")
        {
            throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: Node already added");
        }
        else if(strCommand == "remove")
        {
            throw JSONRPCError(RPC_CLIENT_NODE_NOT_ADDED, "Error: Node has not been added.");
        }
    }
    return Value::null;
}

Value getaddednodeinfo(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getaddednodeinfo dns ( \"node\" )\n"
            "\nReturns information about the given added node, or all added nodes\n"
            "(note that onetry addnodes are not listed here)\n"
            "If dns is false, only a list of added nodes will be provided,\n"
            "otherwise connected information will also be available.\n"
            "\nArguments:\n"
            "1. dns        (boolean, required) If false, only a list of added nodes will be provided, otherwise connected information will also be available.\n"
            "2. \"node\"   (string, optional) If provided, return information about this specific node, otherwise all nodes are returned.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"addednode\" : \"192.168.0.201\",   (string) The node ip address\n"
            "    \"connected\" : true|false,          (boolean) If connected\n"
            "    \"addresses\" : [\n"
            "       {\n"
            "         \"address\" : \"192.168.0.201:51472\",  (string) The divi server host and port\n"
            "         \"connected\" : \"outbound\"           (string) connection, inbound or outbound\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getaddednodeinfo", "true") + HelpExampleCli("getaddednodeinfo", "true \"192.168.0.201\"") + HelpExampleRpc("getaddednodeinfo", "true, \"192.168.0.201\""));

    bool fDns = params[0].get_bool();

    std::vector<std::string> addedNodes = getAddedNodeList();
    list<string> laddedNodes(0);
    if (params.size() == 1) {
        BOOST_FOREACH (std::string& strAddNode, addedNodes)
            laddedNodes.push_back(strAddNode);
    } else {
        string strNode = params[1].get_str();
        BOOST_FOREACH (std::string& strAddNode, addedNodes)
            if (strAddNode == strNode) {
                laddedNodes.push_back(strAddNode);
                break;
            }
        if (laddedNodes.size() == 0)
            throw JSONRPCError(RPC_CLIENT_NODE_NOT_ADDED, "Error: Node has not been added.");
    }

    Array ret;
    if (!fDns) {
        BOOST_FOREACH (string& strAddNode, laddedNodes) {
            Object obj;
            obj.push_back(Pair("addednode", strAddNode));
            ret.push_back(obj);
        }
        return ret;
    }

    std::list<std::pair<std::string, std::vector<CService> > > laddedAddreses(0);
    BOOST_FOREACH (string& strAddNode, laddedNodes) {
        std::vector<CService> vservNode(0);
        if (Lookup(strAddNode.c_str(), vservNode, Params().GetDefaultPort(), getNameLookupFlag(), 0))
            laddedAddreses.push_back(std::make_pair(strAddNode, vservNode));
        else {
            Object obj;
            obj.push_back(Pair("addednode", strAddNode));
            obj.push_back(Pair("connected", false));
            Array addresses;
            obj.push_back(Pair("addresses", addresses));
        }
    }

    for (std::list<std::pair<std::string, std::vector<CService> > >::iterator it = laddedAddreses.begin(); it != laddedAddreses.end(); it++) {
        Object obj;
        obj.push_back(Pair("addednode", it->first));

        Array addresses;
        bool fConnected = false;
        BOOST_FOREACH (CService& addrNode, it->second) {
            bool fFound = false;
            Object node;
            node.push_back(Pair("address", addrNode.ToString()));
            NodeConnectionStatus connectionStatus = GetConnectionStatus(addrNode);
            if(connectionStatus != NodeConnectionStatus::NOT_CONNECTED)
            {
                fFound = true;
                fConnected = true;
                node.push_back(Pair("connected", (connectionStatus == NodeConnectionStatus::INBOUND)? "inbound" : "outbound"));
            }
            if (!fFound)
                node.push_back(Pair("connected", "false"));
            addresses.push_back(node);
        }
        obj.push_back(Pair("connected", fConnected));
        obj.push_back(Pair("addresses", addresses));
        ret.push_back(obj);
    }

    return ret;
}

Value getnettotals(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || params.size() > 0)
        throw runtime_error(
            "getnettotals\n"
            "\nReturns information about network traffic, including bytes in, bytes out,\n"
            "and current time.\n"
            "\nResult:\n"
            "{\n"
            "  \"totalbytesrecv\": n,   (numeric) Total bytes received\n"
            "  \"totalbytessent\": n,   (numeric) Total bytes sent\n"
            "  \"timemillis\": t        (numeric) Total cpu time\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getnettotals", "") + HelpExampleRpc("getnettotals", ""));

    Object obj;
    obj.push_back(Pair("totalbytesrecv", NetworkUsageStats::GetTotalBytesRecv()));
    obj.push_back(Pair("totalbytessent", NetworkUsageStats::GetTotalBytesSent()));
    obj.push_back(Pair("timemillis", GetTimeMillis()));
    return obj;
}

static Array GetNetworksInfo()
{
    Array networks;
    for (int n = 0; n < NET_MAX; ++n) {
        enum Network network = static_cast<enum Network>(n);
        if (network == NET_UNROUTABLE)
            continue;
        proxyType proxy;
        Object obj;
        GetProxy(network, proxy);
        obj.push_back(Pair("name", GetNetworkName(network)));
        obj.push_back(Pair("limited", IsLimited(network)));
        obj.push_back(Pair("reachable", IsReachable(network)));
        obj.push_back(Pair("proxy", proxy.IsValid() ? proxy.proxy.ToStringIPPort() : string()));
        obj.push_back(Pair("proxy_randomize_credentials", proxy.randomize_credentials));
        networks.push_back(obj);
    }
    return networks;
}

Value getnetworkinfo(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getnetworkinfo\n"
            "Returns an object containing various state info regarding P2P networking.\n"
            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,                      (numeric) the server version\n"
            "  \"subversion\": \"/Divi Core:x.x.x.x/\",     (string) the server subversion string\n"
            "  \"protocolversion\": xxxxx,              (numeric) the protocol version\n"
            "  \"localservices\": \"xxxxxxxxxxxxxxxx\", (string) the services we offer to the network\n"
            "  \"timeoffset\": xxxxx,                   (numeric) the time offset\n"
            "  \"connections\": xxxxx,                  (numeric) the number of connections\n"
            "  \"networks\": [                          (array) information per network\n"
            "  {\n"
            "    \"name\": \"xxx\",                     (string) network (ipv4, ipv6 or onion)\n"
            "    \"limited\": true|false,               (boolean) is the network limited using -onlynet?\n"
            "    \"reachable\": true|false,             (boolean) is the network reachable?\n"
            "    \"proxy\": \"host:port\"               (string) the proxy that is used for this network, or empty if none\n"
            "  }\n"
            "  ,...\n"
            "  ],\n"
            "  \"relayfee\": x.xxxxxxxx,                (numeric) minimum relay fee for non-free transactions in divi/kb\n"
            "  \"localaddresses\": [                    (array) list of local addresses\n"
            "  {\n"
            "    \"address\": \"xxxx\",                 (string) network address\n"
            "    \"port\": xxx,                         (numeric) network port\n"
            "    \"score\": xxx                         (numeric) relative score\n"
            "  }\n"
            "  ,...\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getnetworkinfo", "") + HelpExampleRpc("getnetworkinfo", ""));

    Object obj;
    obj.push_back(Pair("version", CLIENT_VERSION_STR));
    obj.push_back(Pair("subversion",
        FormatSubVersion(std::vector<std::string>())));
    obj.push_back(Pair("protocolversion", PROTOCOL_VERSION));
    obj.push_back(Pair("localservices", strprintf("%016x", GetLocalServices())));
    obj.push_back(Pair("timeoffset", GetTimeOffset()));
    obj.push_back(Pair("connections", (int)GetPeerCount()));
    obj.push_back(Pair("networks", GetNetworksInfo()));
    obj.push_back(Pair("relayfee", ValueFromAmount( FeeAndPriorityCalculator::instance().getMinimumRelayFeeRate().GetFeePerK() )));
    Array localAddresses;
    std::vector<LocalHostData> localHostData = GetLocalHostData();
    for (const LocalHostData& item: localHostData)
    {
        Object rec;
        rec.push_back(Pair("address", item.address));
        rec.push_back(Pair("port", item.port));
        rec.push_back(Pair("score", item.score));
        localAddresses.push_back(rec);
    }
    obj.push_back(Pair("localaddresses", localAddresses));
    return obj;
}
