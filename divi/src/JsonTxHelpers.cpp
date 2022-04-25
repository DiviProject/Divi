#include <JsonTxHelpers.h>
#include <primitives/transaction.h>
#include <utilstrencodings.h>
#include <boost/foreach.hpp>
#include <ChainstateManager.h>
#include <base58address.h>
#include <script/standard.h>
#include <blockmap.h>
#include <chain.h>

namespace Temporary
{
void ScriptPubKeyToJSON(const CScript& scriptPubKey, json_spirit::Object& out, bool fIncludeHex)
{
    txnouttype type;
    std::vector<CTxDestination> addresses;
    int nRequired;

    out.push_back(json_spirit::Pair("asm", scriptPubKey.ToString()));
    if (fIncludeHex)
        out.push_back(json_spirit::Pair("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end())));

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
        out.push_back(json_spirit::Pair("type", GetTxnOutputType(type)));
        return;
    }

    out.push_back(json_spirit::Pair("reqSigs", nRequired));
    out.push_back(json_spirit::Pair("type", GetTxnOutputType(type)));

    json_spirit::Array a;
    BOOST_FOREACH (const CTxDestination& addr, addresses)
        a.push_back(CBitcoinAddress(addr).ToString());
    out.push_back(json_spirit::Pair("addresses", a));
}
}


void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json_spirit::Object& entry)
{
    entry.push_back(json_spirit::Pair("txid", tx.GetHash().GetHex()));
    entry.push_back(json_spirit::Pair("baretxid", tx.GetBareTxid().GetHex()));
    entry.push_back(json_spirit::Pair("version", tx.nVersion));
    entry.push_back(json_spirit::Pair("locktime", (int64_t)tx.nLockTime));
    json_spirit::Array vin;
    BOOST_FOREACH (const CTxIn& txin, tx.vin) {
        json_spirit::Object in;
        if (tx.IsCoinBase())
            in.push_back(json_spirit::Pair("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
        else {
            in.push_back(json_spirit::Pair("txid", txin.prevout.hash.GetHex()));
            in.push_back(json_spirit::Pair("vout", (int64_t)txin.prevout.n));
            json_spirit::Object o;
            o.push_back(json_spirit::Pair("asm", txin.scriptSig.ToString()));
            o.push_back(json_spirit::Pair("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
            in.push_back(json_spirit::Pair("scriptSig", o));
        }
        in.push_back(json_spirit::Pair("sequence", (int64_t)txin.nSequence));
        vin.push_back(in);
    }
    entry.push_back(json_spirit::Pair("vin", vin));
    json_spirit::Array vout;
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& txout = tx.vout[i];
        json_spirit::Object out;
        out.push_back(json_spirit::Pair("value", ValueFromAmount(txout.nValue)));
        out.push_back(json_spirit::Pair("n", (int64_t)i));
        json_spirit::Object o;
        Temporary::ScriptPubKeyToJSON(txout.scriptPubKey, o, true);
        out.push_back(json_spirit::Pair("scriptPubKey", o));
        vout.push_back(out);
    }
    entry.push_back(json_spirit::Pair("vout", vout));

    if (hashBlock != 0) {
        const ChainstateManager::Reference chainstate;
        const auto& chain = chainstate->ActiveChain();
        const auto& blockMap = chainstate->GetBlockMap();

        entry.push_back(json_spirit::Pair("blockhash", hashBlock.GetHex()));
        const auto mi = blockMap.find(hashBlock);
        if (mi != blockMap.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chain.Contains(pindex)) {
                entry.push_back(json_spirit::Pair("confirmations", 1 + chain.Height() - pindex->nHeight));
                entry.push_back(json_spirit::Pair("time", pindex->GetBlockTime()));
                entry.push_back(json_spirit::Pair("blocktime", pindex->GetBlockTime()));
            } else
                entry.push_back(json_spirit::Pair("confirmations", 0));
        }
    }
}
