// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcclient.h"

#include "rpcprotocol.h"
#include "ui_interface.h"
#include "util.h"

#include <set>
#include <stdint.h>

using namespace std;
using namespace json_spirit;

class CRPCConvertParam
{
public:
    std::string methodName; //! method whose params want conversion
    int paramIdx;           //! 0-based idx of param to convert
};
// ***TODO***
static const CRPCConvertParam vRPCConvertParams[] =
    {
        {"stop", 0},
        {"setmocktime", 0},
        {"getaddednodeinfo", 0},
        {"setgenerate", 0},
        {"setgenerate", 1},
        {"getnetworkhashps", 0},
        {"getnetworkhashps", 1},
        {"sendtoaddress", 1},
        {"sendtoaddressix", 1},
        {"settxfee", 0},
        {"getreceivedbyaddress", 1},
        {"getreceivedbyaccount", 1},
        {"listreceivedbyaddress", 0},
        {"listreceivedbyaddress", 1},
        {"listreceivedbyaddress", 2},
        {"listreceivedbyaccount", 0},
        {"listreceivedbyaccount", 1},
        {"listreceivedbyaccount", 2},
        {"getbalance", 1},
        {"getbalance", 2},
        {"getblockhash", 0},
        {"move", 2},
        {"move", 3},
        {"sendfrom", 2},
        {"sendfrom", 3},
        {"listtransactions", 1},
        {"listtransactions", 2},
        {"listtransactions", 3},
        {"listaccounts", 0},
        {"listaccounts", 1},
        {"walletpassphrase", 1},
        {"walletpassphrase", 2},
        {"getblocktemplate", 0},
        {"listsinceblock", 1},
        {"listsinceblock", 2},
        {"sendmany", 1},
        {"sendmany", 2},
        {"addmultisigaddress", 0},
        {"addmultisigaddress", 1},
        {"createmultisig", 0},
        {"createmultisig", 1},
        {"listunspent", 0},
        {"listunspent", 1},
        {"listunspent", 2},
        {"getblock", 1},
        {"getblockheader", 1},
        {"gettransaction", 1},
        {"getrawtransaction", 1},
        {"createrawtransaction", 0},
        {"createrawtransaction", 1},
        {"signrawtransaction", 1},
        {"signrawtransaction", 2},
        {"sendrawtransaction", 1},
        {"gettxout", 1},
        {"gettxout", 2},
        {"lockunspent", 0},
        {"lockunspent", 1},
        {"importprivkey", 2},
        {"importaddress", 2},
        {"verifychain", 0},
        {"verifychain", 1},
        {"keypoolrefill", 0},
        {"getrawmempool", 0},
        {"estimatefee", 0},
        {"estimatepriority", 0},
        {"prioritisetransaction", 1},
        {"prioritisetransaction", 2},
        {"spork", 1},
        {"mnbudget", 3},
        {"mnbudget", 4},
        {"mnbudget", 6},
        {"mnbudget", 8},
        {"preparebudget", 2},
        {"preparebudget", 3},
        {"preparebudget", 5},
        {"submitbudget", 2},
        {"submitbudget", 3},
        {"submitbudget", 5},
        {"submitbudget", 7},
        // disabled until removal of the legacy 'masternode' command
        //{"startmasternode", 1},
        {"mnvoteraw", 1},
        {"mnvoteraw", 4},
        {"reservebalance", 0},
        {"reservebalance", 1},
        {"setstakesplitthreshold", 0},
        {"autocombinerewards", 0},
        {"autocombinerewards", 1},
        {"getzerocoinbalance", 0},
        {"listmintedzerocoins", 0},
        {"listspentzerocoins", 0},
        {"listzerocoinamounts", 0},
        {"mintzerocoin", 0},
        {"spendzerocoin", 0},
        {"spendzerocoin", 1},
        {"spendzerocoin", 2},
        {"spendzerocoin", 3},
        {"importzerocoins", 0},
        {"exportzerocoins", 0},
        {"exportzerocoins", 1},
        {"resetmintzerocoin", 0},
        {"getspentzerocoinamount", 1},

        {"getaddresstxids", 0},
        {"getaddressdeltas", 0},
        {"getaddressbalance", 0},
        {"getaddressutxos", 0},
        {"getaddressmempool", 0},
        {"getspentinfo", 0}
    };

class CRPCConvertTable
{
private:
    std::set<std::pair<std::string, int> > members;

public:
    CRPCConvertTable();

    bool convert(const std::string& method, int idx)
    {
        return (members.count(std::make_pair(method, idx)) > 0);
    }
};

CRPCConvertTable::CRPCConvertTable()
{
    const unsigned int n_elem =
        (sizeof(vRPCConvertParams) / sizeof(vRPCConvertParams[0]));

    for (unsigned int i = 0; i < n_elem; i++) {
        members.insert(std::make_pair(vRPCConvertParams[i].methodName,
            vRPCConvertParams[i].paramIdx));
    }
}

static CRPCConvertTable rpcCvtTable;

/** Convert strings to command-specific RPC representation */
Array RPCConvertValues(const std::string& strMethod, const std::vector<std::string>& strParams)
{
    Array params;

    for (unsigned int idx = 0; idx < strParams.size(); idx++) {
        const std::string& strVal = strParams[idx];

        // insert string value directly
        if (!rpcCvtTable.convert(strMethod, idx)) {
            params.push_back(strVal);
        }

        // parse string as JSON, insert bool/number/object/etc. value
        else {
            Value jVal;
            if (!read_string(strVal, jVal))
                throw runtime_error(string("Error parsing JSON:") + strVal);
            params.push_back(jVal);
        }
    }

    return params;
}
