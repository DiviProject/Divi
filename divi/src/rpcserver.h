// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPCSERVER_H
#define BITCOIN_RPCSERVER_H

#include <boost/asio.hpp>
#include <amount.h>
#include "uint256.h"

#include <list>
#include <map>
#include <stdint.h>
#include <string>

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_utils.h"
#include "json/json_spirit_writer_template.h"
#include <JsonParseHelpers.h>

class CBlockIndex;
class CNetAddr;

/** Start RPC threads */
void StartRPCThreads();
/**
 * Alternative to StartRPCThreads for the GUI, when no server is
 * used. The RPC thread in this case is only used to handle timeouts.
 * If real RPC threads have already been started this is a no-op.
 */
void StartDummyRPCThread();
/** Stop RPC threads */
void StopRPCThreads();
/** Query whether RPC is running */
bool IsRPCRunning();

/**
 * Set the RPC warmup status.  When this is done, all RPC calls will error out
 * immediately with RPC_IN_WARMUP.
 */
void SetRPCWarmupStatus(const std::string& newStatus);
/* Mark warmup as done.  RPC calls will be processed from now on.  */
void SetRPCWarmupFinished();

/* returns the current warmup state.  */
bool RPCIsInWarmup(std::string* statusOut);

/**
 * Type-check arguments; throws JSONRPCError if wrong type given. Does not check that
 * the right number of arguments are passed, just that any passed are the correct type.
 * Use like:  RPCTypeCheck(params, boost::assign::list_of(str_type)(int_type)(obj_type));
 */
void RPCTypeCheck(const json_spirit::Array& params,
    const std::list<json_spirit::Value_type>& typesExpected,
    bool fAllowNull = false);
/**
 * Check for expected keys/value types in an Object.
 * Use like: RPCTypeCheck(object, boost::assign::map_list_of("name", str_type)("value", int_type));
 */
void RPCTypeCheck(const json_spirit::Object& o,
    const std::map<std::string, json_spirit::Value_type>& typesExpected,
    bool fAllowNull = false);

//! Convert boost::asio address to CNetAddr
CNetAddr BoostAsioToCNetAddr(boost::asio::ip::address address);

class CWallet;
typedef json_spirit::Value (*rpcfn_type)(const json_spirit::Array& params, bool fHelp, CWallet* pwallet);

class CRPCCommand
{
public:
    std::string category;
    std::string name;
    rpcfn_type actor;
    bool okSafeMode;
    bool threadSafe;
    bool requiresWalletLock;
    bool requiresWalletInstance;
};

/**
 * DIVI RPC command dispatcher.
 */
class CRPCTable
{
private:
    std::map<std::string, const CRPCCommand*> mapCommands;
    CRPCTable();

public:
    const CRPCCommand* operator[](std::string name) const;
    std::string help(std::string name,CWallet* pwallet) const;

    /**
     * Execute a method.
     * @param method   Method to execute
     * @param params   Array of arguments (JSON objects)
     * @returns Result of the call.
     * @throws an exception (json_spirit::Value) when an error happens.
     */
    json_spirit::Value execute(const std::string& method, const json_spirit::Array& params) const;

    /**
    * Returns a list of registered commands
    * @returns List of registered commands.
    */
    std::vector<std::string> listCommands() const;
    inline static const CRPCTable& getRPCTable()
    {
        static const CRPCTable rpcTable;
        return rpcTable;
    }
};

void EnsureWalletIsUnlocked(CWallet* pwallet);
void LockWallet(CWallet* pwallet);
void UnlockWalletBriefly(CWallet* pwallet, int64_t sleepTime, bool revertToUnlockedForStakingOnExpiry);
int64_t TimeTillWalletLock(CWallet* pwallet);
std::string HelpRequiringPassphrase(CWallet* pwallet);
std::string HelpExampleCli(std::string methodname, std::string args);
std::string HelpExampleRpc(std::string methodname, std::string args);
std::string GetWarningMessage(std::string category);
#endif // BITCOIN_RPCSERVER_H
