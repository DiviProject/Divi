// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"

#include <blockmap.h>
#include <chain.h>
#include <ChainstateManager.h>
#include "core_io.h"
#include <FeeAndPriorityCalculator.h>
#include "init.h"
#include "keystore.h"
#include "net.h"
#include "primitives/transaction.h"
#include <rpcprotocol.h>
#include "rpcserver.h"
#include "script/script.h"
#include "script/sign.h"
#include "script/standard.h"
#include <TransactionDiskAccessor.h>
#include "uint256.h"
#include "utilmoneystr.h"
#include "wallet.h"
#include <WalletTx.h>
#include <txmempool.h>
#include <TransactionSearchIndexes.h>
#include <sync.h>

#include <stdint.h>

#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"
#include <boost/assign/list_of.hpp>
#include <ValidationState.h>
#include <script/SignatureCheckers.h>

#include <JsonTxHelpers.h>

using namespace boost;
using namespace boost::assign;
using namespace json_spirit;
using namespace std;

extern CCriticalSection cs_main;

void TxToJSONExpanded(const CTransaction& tx, const uint256 hashBlock, Object& entry,
                      int nHeight = 0, int nConfirmations = 0, int nBlockTime = 0)
{
    const ChainstateManager::Reference chainstate;
    const auto& blockTree = chainstate->BlockTree();

    uint256 txid = tx.GetHash();
    entry.push_back(Pair("txid", txid.GetHex()));
    entry.push_back(Pair("baretxid", tx.GetBareTxid().GetHex()));
    entry.push_back(Pair("version", tx.nVersion));
    entry.push_back(Pair("locktime", (int64_t)tx.nLockTime));
    Array vin;
    BOOST_FOREACH(const CTxIn& txin, tx.vin) {
        Object in;
        if (tx.IsCoinBase())
            in.push_back(Pair("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
        else {
            in.push_back(Pair("txid", txin.prevout.hash.GetHex()));
            in.push_back(Pair("vout", (int64_t)txin.prevout.n));
            Object o;
            o.push_back(Pair("asm", txin.scriptSig.ToString()));
            o.push_back(Pair("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
            in.push_back(Pair("scriptSig", o));

            // Add address and value info if spentindex enabled
            CSpentIndexValue spentInfo;
            const CSpentIndexKey spentKey(txin.prevout.hash, txin.prevout.n);
            if (TransactionSearchIndexes::GetSpentIndex(&blockTree, spentKey, spentInfo)) {
                in.push_back(Pair("value", ValueFromAmount(spentInfo.satoshis)));
                in.push_back(Pair("valueSat", spentInfo.satoshis));
                if (spentInfo.addressType == 1) {
                    in.push_back(Pair("address", CBitcoinAddress(CKeyID(spentInfo.addressHash)).ToString()));
                } else if (spentInfo.addressType == 2)  {
                    in.push_back(Pair("address", CBitcoinAddress(CScriptID(spentInfo.addressHash)).ToString()));
                }
            }

        }
        in.push_back(Pair("sequence", (int64_t)txin.nSequence));
        vin.push_back(in);
    }
    entry.push_back(Pair("vin", vin));
    Array vout;
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& txout = tx.vout[i];
        Object out;
        out.push_back(Pair("value", ValueFromAmount(txout.nValue)));
        out.push_back(Pair("valueSat", txout.nValue));
        out.push_back(Pair("n", (int64_t)i));
        Object o;
        ScriptPubKeyToJSON(txout.scriptPubKey, o, true);
        out.push_back(Pair("scriptPubKey", o));

        // Add spent information if spentindex is enabled.  We don't know
        // whether or not segwit light is in effect for the transaction,
        // so we simply try looking up by both txid and bare txid as at
        // most one of them can match anyway.
        CSpentIndexValue spentInfo;
        bool found = TransactionSearchIndexes::GetSpentIndex(&blockTree, CSpentIndexKey(txid, i), spentInfo);
        if (!found)
          found = TransactionSearchIndexes::GetSpentIndex(&blockTree, CSpentIndexKey(tx.GetBareTxid(), i), spentInfo);
        if (found) {
            out.push_back(Pair("spentTxId", spentInfo.txid.GetHex()));
            out.push_back(Pair("spentIndex", (int)spentInfo.inputIndex));
            out.push_back(Pair("spentHeight", spentInfo.blockHeight));
        }

        vout.push_back(out);
    }
    entry.push_back(Pair("vout", vout));

    if (!hashBlock.IsNull()) {
        entry.push_back(Pair("blockhash", hashBlock.GetHex()));

        if (nConfirmations > 0) {
            entry.push_back(Pair("height", nHeight));
            entry.push_back(Pair("confirmations", nConfirmations));
            entry.push_back(Pair("time", nBlockTime));
            entry.push_back(Pair("blocktime", nBlockTime));
        } else {
            entry.push_back(Pair("height", -1));
            entry.push_back(Pair("confirmations", 0));
        }
    }

    entry.push_back(Pair("size", static_cast<int64_t>(tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION))));

}

Value getrawtransaction(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getrawtransaction \"txid\" ( verbose )\n"
            "\nNOTE: By default this function only works sometimes. This is when the tx is in the mempool\n"
            "or there is an unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option.\n"
            "\nReturn the raw transaction data.\n"
            "\nIf verbose=0, returns a string that is serialized, hex-encoded data for 'txid'.\n"
            "If verbose is non-zero, returns an Object with information about 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose       (numeric, optional, default=0) If 0, return a string, other return a json object\n"

            "\nResult (if verbose is not set or set to 0):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose > 0):\n"
            "{\n"
            "  \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "  \"baretxid\" : \"baretxid\", (string) The bare txid (without signatures)\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in DIVI\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"diviaddress\"        (string) divi address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"confirmations\" : n,      (numeric) The confirmations\n"
            "  \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getrawtransaction", "\"mytxid\"") + HelpExampleCli("getrawtransaction", "\"mytxid\" 1") + HelpExampleRpc("getrawtransaction", "\"mytxid\", 1"));

    uint256 hash = ParseHashV(params[0], "parameter 1");

    bool fVerbose = false;
    if (params.size() > 1)
        fVerbose = (params[1].get_int() != 0);

    CTransaction tx;
    uint256 hashBlock = 0;
    int nHeight = 0;
    int nConfirmations = 0;
    int nBlockTime = 0;

    {
        LOCK(cs_main);
        if (!GetTransaction(hash, tx, hashBlock, true))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

        const ChainstateManager::Reference chainstate;
        const auto& chain = chainstate->ActiveChain();
        const auto& blockMap = chainstate->GetBlockMap();

        const auto mi = blockMap.find(hashBlock);
        if (mi != blockMap.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chain.Contains(pindex)) {
                nHeight = pindex->nHeight;
                nConfirmations = 1 + chain.Height() - pindex->nHeight;
                nBlockTime = pindex->GetBlockTime();
            } else {
                nHeight = -1;
                nConfirmations = 0;
                nBlockTime = pindex->GetBlockTime();
            }
        }
    }

    string strHex = EncodeHexTx(tx);

    if (!fVerbose)
        return strHex;

    Object result;
    result.push_back(Pair("hex", strHex));
    TxToJSONExpanded(tx, hashBlock, result, nHeight, nConfirmations, nBlockTime);
    return result;
}

#ifdef ENABLE_WALLET
Value listunspent(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
            "listunspent ( minconf maxconf  [\"address\",...] )\n"
            "\nReturns array of unspent transaction outputs\n"
            "with between minconf and maxconf (inclusive) confirmations.\n"
            "Optionally filter to only include txouts paid to specified addresses.\n"
            "Results are an array of Objects, each of which has:\n"
            "{txid, vout, scriptPubKey, amount, confirmations}\n"
            "\nArguments:\n"
            "1. minconf          (numeric, optional, default=1) The minimum confirmations to filter\n"
            "2. maxconf          (numeric, optional, default=9999999) The maximum confirmations to filter\n"
            "3. \"addresses\"    (string) A json array of divi addresses to filter\n"
            "    [\n"
            "      \"address\"   (string) divi address\n"
            "      ,...\n"
            "    ]\n"
            "\nResult\n"
            "[                   (array of json object)\n"
            "  {\n"
            "    \"txid\" : \"txid\",        (string) the transaction id \n"
            "    \"vout\" : n,               (numeric) the vout value\n"
            "    \"address\" : \"address\",  (string) the divi address\n"
            "    \"account\" : \"account\",  (string) The associated account, or \"\" for the default account\n"
            "    \"scriptPubKey\" : \"key\", (string) the script key\n"
            "    \"amount\" : x.xxx,         (numeric) the transaction amount in DIVI\n"
            "    \"confirmations\" : n       (numeric) The number of confirmations\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples\n" +
            HelpExampleCli("listunspent", "") + HelpExampleCli("listunspent", "6 9999999 \"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\"") + HelpExampleRpc("listunspent", "6, 9999999 \"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\""));

    RPCTypeCheck(params, list_of(int_type)(int_type)(array_type));

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    int nMaxDepth = 9999999;
    if (params.size() > 1)
        nMaxDepth = params[1].get_int();

    set<CBitcoinAddress> setAddress;
    if (params.size() > 2) {
        Array inputs = params[2].get_array();
        BOOST_FOREACH (Value& input, inputs) {
            CBitcoinAddress address(input.get_str());
            if (!address.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid DIVI address: ") + input.get_str());
            if (setAddress.count(address))
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ") + input.get_str());
            setAddress.insert(address);
        }
    }

    Array results;
    std::vector<COutput> vecOutputs;
    if(!pwallet)
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"No wallet is enabled");
    }
    pwallet->AvailableCoins(vecOutputs, false);
    const AddressBook& addressBook = pwallet->getAddressBookManager().getAddressBook();
    BOOST_FOREACH (const COutput& out, vecOutputs) {
        if (out.nDepth < nMinDepth || out.nDepth > nMaxDepth)
            continue;

        if (setAddress.size()) {
            CTxDestination address;
            if (!ExtractDestination(out.tx->vout[out.i].scriptPubKey, address))
                continue;

            if (!setAddress.count(address))
                continue;
        }

        CAmount nValue = out.tx->vout[out.i].nValue;
        const CScript& pk = out.tx->vout[out.i].scriptPubKey;
        Object entry;
        entry.push_back(Pair("txid", out.tx->GetHash().GetHex()));
        entry.push_back(Pair("vout", out.i));
        CTxDestination address;
        if (ExtractDestination(out.tx->vout[out.i].scriptPubKey, address)) {
            entry.push_back(Pair("address", CBitcoinAddress(address).ToString()));
            if (addressBook.count(address))
                entry.push_back(Pair("account", addressBook.find(address)->second.name));
        }
        entry.push_back(Pair("scriptPubKey", HexStr(pk.begin(), pk.end())));
        if (pk.IsPayToScriptHash()) {
            CTxDestination address;
            if (ExtractDestination(pk, address)) {
                const CScriptID& hash = boost::get<CScriptID>(address);
                CScript redeemScript;
                if (pwallet->GetCScript(hash, redeemScript))
                    entry.push_back(Pair("redeemScript", HexStr(redeemScript.begin(), redeemScript.end())));
            }
        }
        entry.push_back(Pair("amount", ValueFromAmount(nValue)));
        entry.push_back(Pair("confirmations", out.nDepth));
        entry.push_back(Pair("spendable", out.fSpendable));
        results.push_back(entry);
    }

    return results;
}
#endif

Value createrawtransaction(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "createrawtransaction [{\"txid\":\"id\",\"vout\":n},...] {\"address\":amount,...}\n"
            "\nCreate a transaction spending the given inputs and sending to the given addresses.\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.\n"

            "\nArguments:\n"
            "1. \"transactions\"        (string, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",  (string, required) The transaction id\n"
            "         \"vout\":n        (numeric, required) The output number\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "2. \"outputs\"             (array, required) array or json object with outputs\n"
            "    [\n"
            "      {\n"
            "        \"address\": x.xxx   (numeric, required) The key is the divi address, the value is the DIVI amount\n"
            "      }, ...\n"
            "      {\n"
            "        \"hex\": x.xxx       (numeric, required) The key is a hex-serialised script, the value is the DIVI amount\n"
            "      }, ...\n"
            "    ]\n"

            "\nResult:\n"
            "\"transaction\"            (string) hex string of the transaction\n"

            "\nExamples\n" +
            HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"") + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"address\\\":0.01}\""));

    RPCTypeCheck(params, list_of(array_type));

    Array inputs = params[0].get_array();

    Array sendTo;
    if (params[1].type() == array_type)
        sendTo = params[1].get_array();
    else if (params[1].type() == obj_type) {
        for (const Pair& s : params[1].get_obj()) {
            Object cur;
            cur.push_back(s);
            sendTo.push_back(cur);
        }
    } else
        throw JSONRPCError(RPC_TYPE_ERROR, "outputs must be array or object");

    CMutableTransaction rawTx;

    for (const Value& input : inputs) {
        const Object& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const Value& vout_v = find_value(o, "vout");
        if (vout_v.type() != int_type)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        rawTx.vin.emplace_back(COutPoint(txid, nOutput));
    }

    set<CBitcoinAddress> setAddress;
    for (const Value& entry : sendTo) {
        if (entry.type() != obj_type)
            throw JSONRPCError(RPC_TYPE_ERROR, "output entries must be objects");
        const Object o = entry.get_obj();
        if (o.size() != 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "output entries must have a single element");

        for (const Pair& s : o) {
            const CAmount nAmount = AmountFromValue(s.value_, true);
            CScript scriptPubKey;

            if (IsHex(s.name_) || s.name_ == "") {
                const auto raw = ParseHex(s.name_);
                scriptPubKey = CScript(raw.begin(), raw.end());
            } else {
                CBitcoinAddress address(s.name_);
                if (!address.IsValid())
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid DIVI address: ") + s.name_);

                if (setAddress.count(address))
                    throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ") + s.name_);
                setAddress.insert(address);

                scriptPubKey = GetScriptForDestination(address.Get());
            }

            CTxOut out(nAmount, scriptPubKey);
            rawTx.vout.push_back(out);
        }
    }

    return EncodeHexTx(rawTx);
}

Value decoderawtransaction(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decoderawtransaction \"hexstring\"\n"
            "\nReturn a JSON object representing the serialized, hex-encoded transaction.\n"

            "\nArguments:\n"
            "1. \"hex\"      (string, required) The transaction hex string\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"id\",        (string) The transaction id\n"
            "  \"baretxid\" : \"baretxid\", (string) The bare txid (without signatures)\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) The output number\n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n     (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [             (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in DIVI\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"12tvKAXCxZjSmdNbao16dKXC8tRWfcF5oc\"   (string) divi address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("decoderawtransaction", "\"hexstring\"") + HelpExampleRpc("decoderawtransaction", "\"hexstring\""));

    RPCTypeCheck(params, list_of(str_type));

    CTransaction tx;

    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    Object result;
    TxToJSON(tx, 0, result);

    return result;
}

Value decodescript(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decodescript \"hex\"\n"
            "\nDecode a hex-encoded script.\n"
            "\nArguments:\n"
            "1. \"hex\"     (string) the hex encoded script\n"
            "\nResult:\n"
            "{\n"
            "  \"asm\":\"asm\",   (string) Script public key\n"
            "  \"hex\":\"hex\",   (string) hex encoded public key\n"
            "  \"type\":\"type\", (string) The output type\n"
            "  \"reqSigs\": n,    (numeric) The required signatures\n"
            "  \"addresses\": [   (json array of string)\n"
            "     \"address\"     (string) divi address\n"
            "     ,...\n"
            "  ],\n"
            "  \"p2sh\",\"address\" (string) script address\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("decodescript", "\"hexstring\"") + HelpExampleRpc("decodescript", "\"hexstring\""));

    RPCTypeCheck(params, list_of(str_type));

    Object r;
    CScript script;
    if (params[0].get_str().size() > 0) {
        std::vector<unsigned char> scriptData(ParseHexV(params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid
    }
    ScriptPubKeyToJSON(script, r, false);

    r.push_back(Pair("p2sh", CBitcoinAddress(CScriptID(script)).ToString()));
    return r;
}

Value signtransactionwithaddresskey(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || false)
        throw runtime_error("signTransactionWithAddressKey\n");

    std::vector<unsigned char> txData(ParseHexV(params[0], "Transaction hex data"));
    const int inputIndex = params[2].get_int();

    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    std::vector<CMutableTransaction> txVariants;
    while (!ssData.empty()) {
        try {
            CMutableTransaction tx;
            ssData >> tx;
            txVariants.push_back(tx);
        } catch (const std::exception&) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        }
    }
    CKeyID keyID;
    CBitcoinAddress address(params[1].get_str());
    address.GetKeyID(keyID);
    CPubKey pubkey;
    if (pwallet && !pwallet->GetPubKey(keyID, pubkey)) throw JSONRPCError(RPC_WALLET_ERROR,"Unable to sign for unknown address");

    CMutableTransaction mergedTx(txVariants[0]);
    CScript dummyOutputScript = CScript() << pubkey << OP_CHECKSIG;
    const CTxOut dummyOutput(0,dummyOutputScript);
    const CKeyStore& keyStore = *pwallet;
    if(!SignForOutput(keyStore, dummyOutput, mergedTx, inputIndex, SIGHASH_SINGLE))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Unable to sign for input");
    }
    return mergedTx.vin[inputIndex].scriptSig.ToString();
}

Value signrawtransaction(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
            "signrawtransaction \"hexstring\" ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] [\"privatekey1\",...] sighashtype )\n"
            "\nSign inputs for raw transaction (serialized, hex-encoded).\n"
            "The second optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the block chain.\n"
            "The third optional argument (may be null) is an array of base58-encoded private\n"
            "keys that, if given, will be the only keys used to sign the transaction.\n"
#ifdef ENABLE_WALLET
            + HelpRequiringPassphrase(pwallet) + "\n"
#endif
            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The transaction hex string\n"
            "2. \"prevtxs\"       (string, optional) An json array of previous dependent transaction outputs\n"
            "     [               (json array of json objects, or 'null' if none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",             (string, required) The transaction id\n"
            "         \"vout\":n,                  (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",   (string, required) script key\n"
            "         \"redeemScript\": \"hex\"    (string, required for P2SH) redeem script\n"
            "       }\n"
            "       ,...\n"
            "    ]\n"
            "3. \"privatekeys\"     (string, optional) A json array of base58-encoded private keys for signing\n"
            "    [                  (json array of strings, or 'null' if none provided)\n"
            "      \"privatekey\"   (string) private key in base58-encoding\n"
            "      ,...\n"
            "    ]\n"
            "4. \"sighashtype\"     (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\": \"value\",   (string) The raw transaction with signature(s) (hex-encoded string)\n"
            "  \"complete\": n       (numeric) if transaction has a complete set of signature (0 if not)\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("signrawtransaction", "\"myhex\"") + HelpExampleRpc("signrawtransaction", "\"myhex\""));

    RPCTypeCheck(params, list_of(str_type)(array_type)(array_type)(str_type), true);

    std::vector<unsigned char> txData(ParseHexV(params[0], "Transaction hex data"));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    std::vector<CMutableTransaction> txVariants;
    while (!ssData.empty()) {
        try {
            CMutableTransaction tx;
            ssData >> tx;
            txVariants.push_back(tx);
        } catch (const std::exception&) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        }
    }

    if (txVariants.empty())
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transaction");

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CMutableTransaction mergedTx(txVariants[0]);
    bool fComplete = true;

    // Fetch previous transactions (inputs):
    CCoinsViewBacked temporaryBacking;
    CCoinsViewCache view(&temporaryBacking);
    {
        CTxMemPool& mempool = GetTransactionMemoryPool();
        LOCK(mempool.cs);
        const ChainstateManager::Reference chainstate;
        const CCoinsViewCache& viewChain = chainstate->CoinsTip();
        const CCoinsViewMemPool viewMempool(&viewChain, mempool);
        temporaryBacking.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        BOOST_FOREACH (const CTxIn& txin, mergedTx.vin) {
            const uint256& prevHash = txin.prevout.hash;
            CCoins coins;
            view.AccessCoins(prevHash); // this is certainly allowed to fail
        }

        temporaryBacking.DettachBackend(); // switch back to avoid locking mempool for too long
    }

    bool fGivenKeys = false;
    CBasicKeyStore tempKeystore;
    if (params.size() > 2 && params[2].type() != null_type) {
        fGivenKeys = true;
        Array keys = params[2].get_array();
        BOOST_FOREACH (Value k, keys) {
            CBitcoinSecret vchSecret;
            bool fGood = vchSecret.SetString(k.get_str());
            if (!fGood)
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
            CKey key = vchSecret.GetKey();
            if (!key.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");
            tempKeystore.AddKey(key);
        }
    }
#ifdef ENABLE_WALLET
    else
        EnsureWalletIsUnlocked(pwallet);
#endif

    // Add previous txouts given in the RPC call:
    if (params.size() > 1 && params[1].type() != null_type) {
        Array prevTxs = params[1].get_array();
        BOOST_FOREACH (Value& p, prevTxs) {
            if (p.type() != obj_type)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");

            Object prevOut = p.get_obj();

            RPCTypeCheck(prevOut, map_list_of("txid", str_type)("vout", int_type)("scriptPubKey", str_type));

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            std::vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                CCoinsModifier coins = view.ModifyCoins(txid);
                if (coins->IsAvailable(nOut) && coins->vout[nOut].scriptPubKey != scriptPubKey) {
                    string err("Previous output scriptPubKey mismatch:\n");
                    err = err + coins->vout[nOut].scriptPubKey.ToString() + "\nvs:\n" +
                          scriptPubKey.ToString();
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                if ((unsigned int)nOut >= coins->vout.size())
                    coins->vout.resize(nOut + 1);
                coins->vout[nOut].scriptPubKey = scriptPubKey;
                coins->vout[nOut].nValue = 0; // we don't know the actual output value
            }

            // if redeemScript given and not using the local wallet (private keys
            // given), add redeemScript to the tempKeystore so it can be signed:
            if (fGivenKeys && scriptPubKey.IsPayToScriptHash()) {
                RPCTypeCheck(prevOut, map_list_of("txid", str_type)("vout", int_type)("scriptPubKey", str_type)("redeemScript", str_type));
                Value v = find_value(prevOut, "redeemScript");
                if (!(v == Value::null)) {
                    std::vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    tempKeystore.AddCScript(redeemScript);
                }
            }
        }
    }

#ifdef ENABLE_WALLET
    const CKeyStore& keystore = ((fGivenKeys || !pwallet) ? tempKeystore : *pwallet);
#else
    const CKeyStore& keystore = tempKeystore;
#endif

    int nHashType = SIGHASH_ALL;
    if (params.size() > 3 && params[3].type() != null_type) {
        static map<string, int> mapSigHashValues =
            boost::assign::map_list_of(string("ALL"), int(SIGHASH_ALL))(string("ALL|ANYONECANPAY"), int(SIGHASH_ALL | SIGHASH_ANYONECANPAY))(string("NONE"), int(SIGHASH_NONE))(string("NONE|ANYONECANPAY"), int(SIGHASH_NONE | SIGHASH_ANYONECANPAY))(string("SINGLE"), int(SIGHASH_SINGLE))(string("SINGLE|ANYONECANPAY"), int(SIGHASH_SINGLE | SIGHASH_ANYONECANPAY));
        string strHashType = params[3].get_str();
        if (mapSigHashValues.count(strHashType))
            nHashType = mapSigHashValues[strHashType];
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
    }

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++) {
        CTxIn& txin = mergedTx.vin[i];
        const CCoins* coins = view.AccessCoins(txin.prevout.hash);
        if (coins == NULL || !coins->IsAvailable(txin.prevout.n)) {
            fComplete = false;
            continue;
        }
        const CScript& prevPubKey = coins->vout[txin.prevout.n].scriptPubKey;

        txin.scriptSig.clear();
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mergedTx.vout.size()))
            SignForOutput(keystore, coins->vout[txin.prevout.n], mergedTx, i, nHashType);

        // ... and merge in other signatures:
        BOOST_FOREACH (const CMutableTransaction& txv, txVariants) {
            txin.scriptSig = CombineSignatures(prevPubKey, mergedTx, i, txin.scriptSig, txv.vin[i].scriptSig);
        }
        if (!VerifyScript(txin.scriptSig, coins->vout[txin.prevout.n], STANDARD_SCRIPT_VERIFY_FLAGS, MutableTransactionSignatureChecker(&mergedTx, i)))
            fComplete = false;
    }

    Object result;
    result.push_back(Pair("hex", EncodeHexTx(mergedTx)));
    result.push_back(Pair("complete", fComplete));

    return result;
}

static std::pair<CAmount,bool> ComputeFeeTotalsAndIfInputsAreKnown(const CTransaction& tx)
{
    CTxMemPool& mempool = GetTransactionMemoryPool();
    LOCK(mempool.cs);
    const ChainstateManager::Reference chainstate;
    const CCoinsViewMemPool viewMemPool(&chainstate->CoinsTip(), mempool);
    const CCoinsViewCache view(&viewMemPool);

    if(!view.HaveInputs(tx))
    {
        return std::make_pair(-1,false);
    }
    else
    {
        return std::make_pair(view.GetValueIn(tx) - tx.GetValueOut(),true) ;
    }
}

Value sendrawtransaction(const Array& params, bool fHelp, CWallet* pwallet)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "sendrawtransaction \"hexstring\" ( allowhighfees )\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node and network.\n"
            "\nAlso see createrawtransaction and signrawtransaction calls.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw transaction)\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
            "\nExamples:\n"
            "\nCreate a transaction\n" +
            HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n" + HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n" + HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nAs a json rpc call\n" + HelpExampleRpc("sendrawtransaction", "\"signedhex\""));

    RPCTypeCheck(params, list_of(str_type)(bool_type));

    // parse hex string from parameter
    CTransaction tx;
    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    uint256 hashTx = tx.GetHash();

    bool fOverrideFees = false;
    if (params.size() > 1)
        fOverrideFees = params[1].get_bool();

    const ChainstateManager::Reference chainstate;
    const auto& view = chainstate->CoinsTip();
    CTxMemPool& mempool = GetTransactionMemoryPool();
    const bool fHaveMempool = mempool.exists(hashTx);

    /* We use the UTXO set as heuristic about whether or not a transaction
       has already been confirmed.  This is not 100% accurate (as all outputs
       could have been spent), but useful in practice.  Since we don't know
       whether or not segwit light has been in effect for the transaction,
       we just try locating both txid and bare txid as at most one of them
       can match anyway.  */
    const CCoins* existingCoins = view.AccessCoins(hashTx);
    if (existingCoins == nullptr)
        existingCoins = view.AccessCoins(tx.GetBareTxid());
    const bool fHaveChain = existingCoins && existingCoins->nHeight < 1000000000;
    static const CFeeRate& feeRate = FeeAndPriorityCalculator::instance().getMinimumRelayFeeRate();
    if (!fHaveMempool && !fHaveChain) {
        // push to local node and sync with wallets
        std::pair<CAmount,bool> feeTotalsAndStatus = ComputeFeeTotalsAndIfInputsAreKnown(tx);
        if(!feeTotalsAndStatus.second)
        {
            throw JSONRPCError(RPC_TRANSACTION_REJECTED, "Unknown inputs being spent");
        }
        if(!fOverrideFees && feeTotalsAndStatus.first > feeRate.GetMaxTxFee())
        {
            throw JSONRPCError(RPC_TRANSACTION_REJECTED, "Fees being paid are in excess of maximum fee, but fee override is not provided");
        }
        CValidationState state;
        if (!SubmitTransactionToMempool(mempool, tx, state))
        {
            if (state.IsInvalid())
                throw JSONRPCError(RPC_TRANSACTION_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
            else
                throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
        }
    } else if (fHaveChain) {
        throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
    }
    RelayTransactionToAllPeers(tx);

    return hashTx.GetHex();
}
