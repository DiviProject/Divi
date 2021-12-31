// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "base58.h"
#include <chain.h>
#include "chainparams.h"
#include "core_io.h"
#include "init.h"
#include "main.h"
#include "miner.h"
#include "net.h"
#include "rpcserver.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet.h"
#endif
#include <I_CoinMinter.h>
#include <CoinMintingModule.h>
#include <ExtendedBlockFactory.h>
#include <masternode-payments.h>
#include <MasternodeModule.h>
#include <spork.h>
#include <stdint.h>
#include <txmempool.h>
#include <sync.h>

#include <boost/assign/list_of.hpp>

#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"
#include "Settings.h"
#include <ValidationState.h>
#include <coins.h>
#include <FeeRate.h>
#include <FeeAndPriorityCalculator.h>
#include <blockmap.h>

using namespace json_spirit;
using namespace std;
using namespace boost;
using namespace boost::assign;
extern Settings& settings;
extern CWallet* pwalletMain;
extern CCoinsViewCache* pcoinsTip;
extern BlockMap mapBlockIndex;
extern CCriticalSection cs_main;
extern CTxMemPool mempool;
extern CChain chainActive;

LastExtensionTimestampByBlockHeight& mapHashedBlocks = getLastExtensionTimestampByBlockHeight();
#ifdef ENABLE_WALLET
Value setgenerate(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "setgenerate generate ( genproclimit )\n"
            "\nSet 'generate' true or false to turn generation on or off.\n"
            "Generation is limited to 'genproclimit' processors, -1 is unlimited.\n"
            "\nArguments:\n"
            "1. generate         (boolean, required) Set to true to turn on generation, false to turn off.\n"
            "2. genproclimit     (numeric, optional) Set the processor limit for when generation is on. Can be -1 for unlimited.\n"
            "                    Note: in -regtest mode, genproclimit controls how many blocks are generated immediately.\n"
            "\nResult\n"
            "[ blockhashes ]     (array, -regtest only) hashes of blocks generated\n"
            "\nExamples:\n"
            "\nSet the generation on with a limit of one processor\n" +
            HelpExampleCli("setgenerate", "true 1") +
            "\nCheck the setting\n" + HelpExampleCli("getmininginfo", "") +
            "\nTurn off generation\n" + HelpExampleCli("setgenerate", "false") +
            "\nUsing json rpc\n" + HelpExampleRpc("setgenerate", "true, 1"));

    if (pwalletMain == NULL)
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (disabled)");

    bool fGenerate = true;
    if (params.size() > 0)
        fGenerate = params[0].get_bool();

    int nGenProcLimit = -1;
    if (params.size() > 1) {
        nGenProcLimit = params[1].get_int();
        if (nGenProcLimit == 0)
            fGenerate = false;
    }

    // -regtest mode: don't return until nGenProcLimit blocks are generated
    if (fGenerate && Params().MineBlocksOnDemand()) {
        int nHeightStart = 0;
        int nHeightEnd = 0;
        int nHeight = 0;
        int nGenerate = (nGenProcLimit > 0 ? nGenProcLimit : 1);

        { // Don't keep cs_main locked
            LOCK(cs_main);
            nHeightStart = chainActive.Height();
            nHeight = nHeightStart;
            nHeightEnd = nHeightStart + nGenerate;
        }

        Array blockHashes;
        CoinMintingModule mintingModule(
            settings,
            cs_main,
            Params(),
            chainActive,
            mapBlockIndex,
            GetMasternodeModule(),
            FeeAndPriorityCalculator::instance().getMinimumRelayFeeRate(),
            pcoinsTip,
            mempool,
            GetPeerBlockNotifyService(),
            *pwalletMain,
            mapHashedBlocks,
            GetSporkManager());
        I_CoinMinter& minter = mintingModule.coinMinter();
        minter.setMintingRequestStatus(true);

        while (nHeight < nHeightEnd)
        {
            const bool newBlockAdded = minter.createNewBlock();
            nHeight +=  newBlockAdded;

            if (!newBlockAdded)
                throw JSONRPCError(RPC_VERIFY_ERROR, "failed to generate a valid block");

            // Don't keep cs_main locked
            LOCK(cs_main);
            if(nHeight == chainActive.Height())
                blockHashes.push_back(chainActive.Tip()->GetBlockHash().GetHex());
        }
        return blockHashes;
    }
    else // Not -regtest: start generate thread, return immediately
    {
        const int numberOfThreadsToSpawn = nGenProcLimit;
        SetPoWThreadPool(pwalletMain, numberOfThreadsToSpawn);
    }

    return Value::null;
}

Value generateblock(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "generateblock ( options )\n"
            "\nTry to generate a single block with some extra options.\n"
            "This is intended for regtest mode and testing.\n"
            "\nArguments:\n"
            "1. options          (object, optional) extra options for generating the block\n"
            "    {\n"
            "      \"extratx\" : [\"hex\", ...],   (array of strings, optional) transactions to include as hex\n"
            "      \"coinstake\" : \"hex\",        (string, optional) coinstake transaction to use as hex\n"
            "      \"ignoreMempool\" : true,       (bool, optional) if set, do not include mempool transactions\n"
            "    }\n"
            "\nResult\n"
            "blockhash     (string) hash of the generated block\n"
            "\nExamples:\n" +
            HelpExampleCli("generateblock", "") + HelpExampleRpc("generateblock", ""));

    RPCTypeCheck(params, list_of(obj_type), true);
    Object options;
    if (params.size() > 0)
        options = params[0].get_obj();
    RPCTypeCheck(options, map_list_of("extratx", array_type)("coinstake", str_type)("ignoreMempool", bool_type), true);

    if (pwalletMain == NULL)
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (disabled)");

    int nHeight = 0;

    { // Don't keep cs_main locked
        LOCK(cs_main);
        nHeight = chainActive.Height();
    }

    CoinMintingModule mintingModule(
        settings,
        cs_main,
        Params(),
        chainActive,
        mapBlockIndex,
        GetMasternodeModule(),
        FeeAndPriorityCalculator::instance().getMinimumRelayFeeRate(),
        pcoinsTip,
        mempool,
        GetPeerBlockNotifyService(),
        *pwalletMain,
        mapHashedBlocks,
        GetSporkManager());
    I_CoinMinter& minter = mintingModule.coinMinter();
    minter.setMintingRequestStatus(true);
    ExtendedBlockFactory* blockFactory = dynamic_cast<ExtendedBlockFactory*>(&mintingModule.blockFactory());
    assert(blockFactory);

    const Value& extraTx = find_value(options, "extratx");
    if (extraTx.type() != null_type)
        for (const Value& val : extraTx.get_array()) {
            CTransaction tx;
            if (!DecodeHexTx(tx, val.get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
            blockFactory->addExtraTransaction(tx);
        }

    const Value& coinstake = find_value(options, "coinstake");
    if (coinstake.type() != null_type) {
        CTransaction tx;
        if (!DecodeHexTx(tx, coinstake.get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        if (!tx.IsCoinStake())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "TX is not a coinstake");
        blockFactory->setCustomCoinstake(tx);
    }

    const Value& ignoreMempool = find_value(options, "ignoreMempool");
    if (ignoreMempool.type() != null_type)
        blockFactory->setIgnoreMempool(ignoreMempool.get_bool());

    const bool newBlockAdded = minter.createNewBlock();

    // Don't keep cs_main locked
    LOCK(cs_main);

    if (!newBlockAdded || chainActive.Height() != nHeight + 1)
        throw JSONRPCError(RPC_VERIFY_ERROR, "failed to generate a valid block");

    return chainActive.Tip()->GetBlockHash().GetHex();
}
#endif


Value getmininginfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getmininginfo\n"
            "\nReturns a json object containing mining-related information."
            "\nResult:\n"
            "{\n"
            "  \"blocks\": nnn,             (numeric) The current block\n"
            "  \"currentblocksize\": nnn,   (numeric) The last block size\n"
            "  \"currentblocktx\": nnn,     (numeric) The last block transaction\n"
            "  \"difficulty\": xxx.xxxxx    (numeric) The current difficulty\n"
            "  \"errors\": \"...\"          (string) Current errors\n"
            "  \"generate\": true|false     (boolean) If the generation is on or off (see setgenerate calls)\n"
            "  \"genproclimit\": n          (numeric) The processor limit for generation. -1 if no generation. (see setgenerate calls)\n"
            "  \"hashespersec\": n          (numeric) The hashes per second of the generation, or 0 if no generation.\n"
            "  \"pooledtx\": n              (numeric) The size of the mem pool\n"
            "  \"testnet\": true|false      (boolean) If using testnet or not\n"
            "  \"chain\": \"xxxx\",         (string) current network name as defined in BIP70 (main, test, regtest)\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getmininginfo", "") + HelpExampleRpc("getmininginfo", ""));

    Object obj;
    obj.push_back(Pair("blocks", (int)chainActive.Height()));
    obj.push_back(Pair("difficulty", (double)GetDifficulty()));
    obj.push_back(Pair("errors", GetWarnings("statusbar")));
    obj.push_back(Pair("mining", (int)settings.GetArg("-mining", false) ));
    obj.push_back(Pair("mining_threads", (int)settings.GetArg("-mining_threads", Params().DefaultMinerThreads() )));
    obj.push_back(Pair("pooledtx", (uint64_t)mempool.size()));
    obj.push_back(Pair("testnet", Params().NetworkID() == CBaseChainParams::TESTNET  ));
    obj.push_back(Pair("chain", Params().NetworkIDString()));
    return obj;
}


// NOTE: Unlike wallet RPC (which use DIVI values), mining RPCs follow GBT (BIP 22) in using satoshi amounts
Value prioritisetransaction(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "prioritisetransaction <txid> <priority delta> <fee delta>\n"
            "Accepts the transaction into mined blocks at a higher (or lower) priority\n"
            "\nArguments:\n"
            "1. \"txid\"       (string, required) The transaction id.\n"
            "2. priority delta (numeric, required) The priority to add or subtract.\n"
            "                  The transaction selection algorithm considers the tx as it would have a higher priority.\n"
            "                  (priority of a transaction is calculated: coinage * value_in_duffs / txsize) \n"
            "3. fee delta      (numeric, required) The fee value (in duffs) to add (or subtract, if negative).\n"
            "                  The fee is not actually paid, only the algorithm for selecting transactions into a block\n"
            "                  considers the transaction as it would have paid a higher (or lower) fee.\n"
            "\nResult\n"
            "true              (boolean) Returns true\n"
            "\nExamples:\n" +
            HelpExampleCli("prioritisetransaction", "\"txid\" 0.0 10000") + HelpExampleRpc("prioritisetransaction", "\"txid\", 0.0, 10000"));

    uint256 hash = ParseHashStr(params[0].get_str(), "txid");

    CAmount nAmount = params[2].get_int64();

    mempool.PrioritiseTransaction(hash, nAmount);
    return true;
}

Value getblocktemplate(const Array& params, bool fHelp)
{
    throw std::runtime_error("getblocktemplate has been removed, use divid for staking");
}