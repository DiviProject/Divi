// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "base58.h"
#include "BlockFactory.h"
#include "BlockTemplate.h"
#include "chainparams.h"
#include "core_io.h"
#include "init.h"
#include "main.h"
#include "miner.h"
#include "net.h"
#include "pow.h"
#include "rpcserver.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "db.h"
#include "wallet.h"
#endif
#include <CoinMinter.h>
#include <masternode-sync.h>

#include <stdint.h>

#include <boost/assign/list_of.hpp>

#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"
#include "Settings.h"
#include <ValidationState.h>

extern NotificationInterfaceRegistry registry;//TODO: rid this
using namespace json_spirit;
using namespace std;
using namespace boost;
using namespace boost::assign;
extern Settings& settings;

/**
 * Return average network hashes per second based on the last 'lookup' blocks,
 * or from the last difficulty change if 'lookup' is nonpositive.
 * If 'height' is nonnegative, compute the estimate at the time when a given block was found.
 */
Value GetNetworkHashPS(int lookup, int height)
{
    CBlockIndex* pb = chainActive.Tip();

    if (height >= 0 && height < chainActive.Height())
        pb = chainActive[height];

    if (pb == NULL || !pb->nHeight)
        return 0;

    // If lookup is -1, then use blocks since last difficulty change.
    if (lookup <= 0)
        lookup = pb->nHeight % 2016 + 1;

    // If lookup is larger than chain, then set it to chain length.
    if (lookup > pb->nHeight)
        lookup = pb->nHeight;

    CBlockIndex* pb0 = pb;
    int64_t minTime = pb0->GetBlockTime();
    int64_t maxTime = minTime;
    for (int i = 0; i < lookup; i++) {
        pb0 = pb0->pprev;
        int64_t time = pb0->GetBlockTime();
        minTime = std::min(time, minTime);
        maxTime = std::max(time, maxTime);
    }

    // In case there's a situation where minTime == maxTime, we don't want a divide by zero exception.
    if (minTime == maxTime)
        return 0;

    uint256 workDiff = pb->nChainWork - pb0->nChainWork;
    int64_t timeDiff = maxTime - minTime;

    return (int64_t)(workDiff.getdouble() / timeDiff);
}

Value getnetworkhashps(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "getnetworkhashps ( blocks height )\n"
            "\nReturns the estimated network hashes per second based on the last n blocks.\n"
            "Pass in [blocks] to override # of blocks, -1 specifies since last difficulty change.\n"
            "Pass in [height] to estimate the network speed at the time when a certain block was found.\n"
            "\nArguments:\n"
            "1. blocks     (numeric, optional, default=120) The number of blocks, or -1 for blocks since last difficulty change.\n"
            "2. height     (numeric, optional, default=-1) To estimate at the time of the given height.\n"
            "\nResult:\n"
            "x             (numeric) Hashes per second estimated\n"
            "\nExamples:\n" +
            HelpExampleCli("getnetworkhashps", "") + HelpExampleRpc("getnetworkhashps", ""));

    return GetNetworkHashPS(params.size() > 0 ? params[0].get_int() : 120, params.size() > 1 ? params[1].get_int() : -1);
}

#ifdef ENABLE_WALLET
Value getgenerate(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getgenerate\n"
            "\nReturn if the server is set to generate coins or not. The default is false.\n"
            "It is set with the command line argument -gen (or divi.conf setting gen)\n"
            "It can also be set with the setgenerate call.\n"
            "\nResult\n"
            "true|false      (boolean) If the server is set to generate coins or not\n"
            "\nExamples:\n" +
            HelpExampleCli("getgenerate", "") + HelpExampleRpc("getgenerate", ""));

    return settings.GetBoolArg("-gen", false);
}


Value setgenerate(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "setgenerate generate ( genproclimit )\n"
            "\nSet 'generate' true or false to turn generation on or off.\n"
            "Generation is limited to 'genproclimit' processors, -1 is unlimited.\n"
            "See the getgenerate call for the current setting.\n"
            "\nArguments:\n"
            "1. generate         (boolean, required) Set to true to turn on generation, false to turn off.\n"
            "2. genproclimit     (numeric, optional) Set the processor limit for when generation is on. Can be -1 for unlimited.\n"
            "                    Note: in -regtest mode, genproclimit controls how many blocks are generated immediately.\n"
            "\nResult\n"
            "[ blockhashes ]     (array, -regtest only) hashes of blocks generated\n"
            "\nExamples:\n"
            "\nSet the generation on with a limit of one processor\n" +
            HelpExampleCli("setgenerate", "true 1") +
            "\nCheck the setting\n" + HelpExampleCli("getgenerate", "") +
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
        int64_t coinstakeSearchInterval;
        const CChainParams& chainParameters = Params();
        BlockFactory blockFactory(*pwalletMain,coinstakeSearchInterval,mapHashedBlocks,chainActive,chainParameters, mempool,cs_main);
        CoinMinter minter(blockFactory,pwalletMain, chainActive, chainParameters,vNodes,masternodeSync,mapHashedBlocks,mempool,cs_main,coinstakeSearchInterval);
        while (nHeight < nHeightEnd)
        {
            const bool fProofOfStake = (nHeight >= Params().LAST_POW_BLOCK());

            unsigned int nExtraNonce = 0;
            const bool newBlockAdded = minter.createNewBlock(nExtraNonce, fProofOfStake);
            nHeight +=  newBlockAdded;

            if (!newBlockAdded)
                throw JSONRPCError(RPC_VERIFY_ERROR, "failed to generate a valid block");

            // Don't keep cs_main locked
            LOCK(cs_main);
            if(nHeight == chainActive.Height())
                blockHashes.push_back(chainActive.Tip()->GetBlockHash().GetHex());
        }
        return blockHashes;
    } else // Not -regtest: start generate thread, return immediately
    {
        SetParameter("-gen", (fGenerate ? "1" : "0"));
        SetParameter("-genproclimit", itostr(nGenProcLimit));
        GenerateDivi(fGenerate, pwalletMain, nGenProcLimit);
    }

    return Value::null;
}

namespace
{

/** Custom BlockFactory that allows more control over the generated block,
 *  to implement the extra options needed with generateblock.  */
class ExtendedBlockFactory : public I_BlockFactory
{

private:
    std::unique_ptr<BlockFactory> blockFactory_;
    /** A list of extra transactions to include in generated blocks, in addition
     *  to the normal mempool collection logic (and without explicit
     *  verififaction).  */
    std::vector<CTransaction> extraTransactions_;

    /** If set, use this transaction as coinstake instead of a generated one.  */
    std::unique_ptr<CTransaction> customCoinstake_;

public:
    ExtendedBlockFactory(
        CWallet& wallet,
        int64_t& lastCoinstakeSearchInterval,
        std::map<unsigned int, unsigned int>& hashedBlockTimestamps,
        CChain& chain,
        const CChainParams& chainParameters,
        CTxMemPool& mempool,
        AnnotatedMixin<boost::recursive_mutex>& mainCS
        ): blockFactory_(new BlockFactory(wallet, lastCoinstakeSearchInterval,hashedBlockTimestamps,chain,chainParameters,mempool,mainCS))
    {
    }

    CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reserveKey, bool fProofOfStake) override
    {
        std::unique_ptr<CBlockTemplate> pblocktemplate(blockFactory_->CreateNewBlockWithKey(reserveKey, fProofOfStake));
        CBlock& block = pblocktemplate->block;

        for (const auto& tx : extraTransactions_)
            block.vtx.push_back(tx);

        if (customCoinstake_ != nullptr) {
            if (!block.IsProofOfStake())
                throw std::runtime_error("trying to set custom coinstake on PoW block");
            assert(block.vtx.size() >= 2);
            CTransaction& coinstake = block.vtx[1];
            assert(coinstake.IsCoinStake());
            coinstake = *customCoinstake_;
        }

        return pblocktemplate.release();
    }

    /** Adds a transaction to be added in addition to standard mempool
     *  collection for the next block that will be created successfully.  */
    void addExtraTransaction(const CTransaction& tx)
    {
        extraTransactions_.push_back(tx);
    }

    /** Sets a transaction to use as coinstake on the generated block.
     *  It is up to the caller to ensure that it actually meets the hash
     *  target, e.g. because a large enough coinage and the minimal difficulty
     *  on regtest meet the target always.  */
    void setCustomCoinstake(const CTransaction& tx)
    {
        customCoinstake_.reset(new CTransaction(tx));
    }

};

} // anonymous namespace

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
            "      \"coinstake\" : \"hex\"         (string, optional) coinstake transaction to use as hex\n"
            "    }\n"
            "\nResult\n"
            "blockhash     (string) hash of the generated block\n"
            "\nExamples:\n" +
            HelpExampleCli("generateblock", "") + HelpExampleRpc("generateblock", ""));

    RPCTypeCheck(params, list_of(obj_type), true);
    Object options;
    if (params.size() > 0)
        options = params[0].get_obj();
    RPCTypeCheck(options, map_list_of("extratx", array_type)("coinstake", str_type), true);

    if (pwalletMain == NULL)
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (disabled)");

    int nHeight = 0;

    { // Don't keep cs_main locked
        LOCK(cs_main);
        nHeight = chainActive.Height();
    }

    int64_t coinstakeSearchInterval;
    const CChainParams& chainParameters = Params();
    ExtendedBlockFactory blockFactory(*pwalletMain,coinstakeSearchInterval,mapHashedBlocks,chainActive,chainParameters, mempool,cs_main);
    CoinMinter minter(blockFactory,pwalletMain, chainActive, chainParameters, vNodes, masternodeSync, mapHashedBlocks, mempool, cs_main, coinstakeSearchInterval);

    const Value& extraTx = find_value(options, "extratx");
    if (extraTx.type() != null_type)
        for (const Value& val : extraTx.get_array()) {
            CTransaction tx;
            if (!DecodeHexTx(tx, val.get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
            blockFactory.addExtraTransaction(tx);
        }

    const Value& coinstake = find_value(options, "coinstake");
    if (coinstake.type() != null_type) {
        CTransaction tx;
        if (!DecodeHexTx(tx, coinstake.get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        if (!tx.IsCoinStake())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "TX is not a coinstake");
        blockFactory.setCustomCoinstake(tx);
    }

    const bool fProofOfStake = (nHeight >= Params().LAST_POW_BLOCK());

    unsigned int nExtraNonce = 0;
    const bool newBlockAdded = minter.createNewBlock(nExtraNonce, fProofOfStake);

    // Don't keep cs_main locked
    LOCK(cs_main);

    if (!newBlockAdded || chainActive.Height() != nHeight + 1)
        throw JSONRPCError(RPC_VERIFY_ERROR, "failed to generate a valid block");

    return chainActive.Tip()->GetBlockHash().GetHex();
}

double dHashesPerSec;
int64_t nHPSTimerStart;
Value gethashespersec(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "gethashespersec\n"
            "\nReturns a recent hashes per second performance measurement while generating.\n"
            "See the getgenerate and setgenerate calls to turn generation on and off.\n"
            "\nResult:\n"
            "n            (numeric) The recent hashes per second when generation is on (will return 0 if generation is off)\n"
            "\nExamples:\n" +
            HelpExampleCli("gethashespersec", "") + HelpExampleRpc("gethashespersec", ""));

    if (GetTimeMillis() - nHPSTimerStart > 8000)
        return (int64_t)0;
    return (int64_t)dHashesPerSec;
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
            "  \"generate\": true|false     (boolean) If the generation is on or off (see getgenerate or setgenerate calls)\n"
            "  \"genproclimit\": n          (numeric) The processor limit for generation. -1 if no generation. (see getgenerate or setgenerate calls)\n"
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
    obj.push_back(Pair("genproclimit", (int)settings.GetArg("-genproclimit", -1)));
    obj.push_back(Pair("networkhashps", getnetworkhashps(params, false)));
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

    mempool.PrioritiseTransaction(hash, params[0].get_str(), params[1].get_real(), nAmount);
    return true;
}


// NOTE: Assumes a conclusive result; if result is inconclusive, it must be handled by caller
static Value BIP22ValidationResult(const CValidationState& state)
{
    if (state.IsValid())
        return Value::null;

    std::string strRejectReason = state.GetRejectReason();
    if (state.IsError())
        throw JSONRPCError(RPC_VERIFY_ERROR, strRejectReason);
    if (state.IsInvalid()) {
        if (strRejectReason.empty())
            return "rejected";
        return strRejectReason;
    }
    // Should be impossible
    return "valid?";
}

Value getblocktemplate(const Array& params, bool fHelp)
{
    throw std::runtime_error("getblocktemplate has been removed, use divid for staking");
}

class submitblock_StateCatcher : public NotificationInterface
{
public:
    uint256 hash;
    bool found;
    CValidationState state;

    submitblock_StateCatcher(const uint256& hashIn) : hash(hashIn), found(false), state(){};

protected:
    virtual void BlockChecked(const CBlock& block, const CValidationState& stateIn)
    {
        if (block.GetHash() != hash)
            return;
        found = true;
        state = stateIn;
    };
};

Value submitblock(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "submitblock \"hexdata\" ( \"jsonparametersobject\" )\n"
            "\nAttempts to submit new block to network.\n"
            "The 'jsonparametersobject' parameter is currently ignored.\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n"

            "\nArguments\n"
            "1. \"hexdata\"    (string, required) the hex-encoded block data to submit\n"
            "2. \"jsonparametersobject\"     (string, optional) object of optional parameters\n"
            "    {\n"
            "      \"workid\" : \"id\"    (string, optional) if the server provided a workid, it MUST be included with submissions\n"
            "    }\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("submitblock", "\"mydata\"") + HelpExampleRpc("submitblock", "\"mydata\""));

    CBlock block;
    if (!DecodeHexBlk(block, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

    uint256 hash = block.GetHash();
    bool fBlockPresent = false;
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end()) {
            CBlockIndex* pindex = mi->second;
            if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                return "duplicate";
            if (pindex->nStatus & BLOCK_FAILED_MASK)
                return "duplicate-invalid";
            // Otherwise, we might only have the header - process the block before returning
            fBlockPresent = true;
        }
    }

    CValidationState state;
    submitblock_StateCatcher sc(block.GetHash());
    RegisterValidationInterface(&sc);
    bool fAccepted = ProcessNewBlock(state, NULL, &block);
    UnregisterValidationInterface(&sc);
    if (fBlockPresent) {
        if (fAccepted && !sc.found)
            return "duplicate-inconclusive";
        return "duplicate";
    }
    if (fAccepted) {
        if (!sc.found)
            return "inconclusive";
        state = sc.state;
    }
    return BIP22ValidationResult(state);
}

Value estimatefee(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "estimatefee nblocks\n"
            "\nEstimates the approximate fee per kilobyte\n"
            "needed for a transaction to begin confirmation\n"
            "within nblocks blocks.\n"
            "\nArguments:\n"
            "1. nblocks     (numeric)\n"
            "\nResult:\n"
            "n :    (numeric) estimated fee-per-kilobyte\n"
            "\n"
            "-1.0 is returned if not enough transactions and\n"
            "blocks have been observed to make an estimate.\n"
            "\nExample:\n" +
            HelpExampleCli("estimatefee", "6"));

    RPCTypeCheck(params, boost::assign::list_of(int_type));

    int nBlocks = params[0].get_int();
    if (nBlocks < 1)
        nBlocks = 1;

    CFeeRate feeRate = mempool.estimateFee(nBlocks);
    if (feeRate == CFeeRate(0))
        return -1.0;

    return ValueFromAmount(feeRate.GetFeePerK());
}

Value estimatepriority(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "estimatepriority nblocks\n"
            "\nEstimates the approximate priority\n"
            "a zero-fee transaction needs to begin confirmation\n"
            "within nblocks blocks.\n"
            "\nArguments:\n"
            "1. nblocks     (numeric)\n"
            "\nResult:\n"
            "n :    (numeric) estimated priority\n"
            "\n"
            "-1.0 is returned if not enough transactions and\n"
            "blocks have been observed to make an estimate.\n"
            "\nExample:\n" +
            HelpExampleCli("estimatepriority", "6"));

    RPCTypeCheck(params, boost::assign::list_of(int_type));

    int nBlocks = params[0].get_int();
    if (nBlocks < 1)
        nBlocks = 1;

    return mempool.estimatePriority(nBlocks);
}
