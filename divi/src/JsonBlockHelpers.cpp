#include <JsonBlockHelpers.h>

#include <primitives/block.h>
#include <chain.h>
#include <version.h>

#include <JsonTxHelpers.h>


double GetDifficulty(const CChain& activeChain, const CBlockIndex* blockindex)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == nullptr) {
        blockindex = activeChain.Tip();
        if (blockindex == nullptr)
            return 1.0;
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29) {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29) {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}


json_spirit::Object blockToJSON(const CChain& activeChain, const CBlock& block, const CBlockIndex* blockindex, bool txDetails)
{
    json_spirit::Object result;
    result.push_back(json_spirit::Pair("hash", block.GetHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (activeChain.Contains(blockindex))
        confirmations = activeChain.Height() - blockindex->nHeight + 1;
    result.push_back(json_spirit::Pair("confirmations", confirmations));
    result.push_back(json_spirit::Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(json_spirit::Pair("height", blockindex->nHeight));
    result.push_back(json_spirit::Pair("version", block.nVersion));
    result.push_back(json_spirit::Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    result.push_back(json_spirit::Pair("acc_checkpoint", block.nAccumulatorCheckpoint.GetHex()));
    json_spirit::Array txs;
    BOOST_FOREACH (const CTransaction& tx, block.vtx) {
        if (txDetails) {
            json_spirit::Object objTx;
            TxToJSON(tx, uint256(0), objTx);
            txs.push_back(objTx);
        } else
            txs.push_back(tx.GetHash().GetHex());
    }
    result.push_back(json_spirit::Pair("tx", txs));
    result.push_back(json_spirit::Pair("time", block.GetBlockTime()));
    result.push_back(json_spirit::Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(json_spirit::Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(json_spirit::Pair("difficulty", GetDifficulty(activeChain, blockindex)));
    result.push_back(json_spirit::Pair("chainwork", blockindex->nChainWork.GetHex()));

    if (blockindex->pprev)
        result.push_back(json_spirit::Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    const CBlockIndex* pnext = activeChain.Next(blockindex);
    if (pnext)
        result.push_back(json_spirit::Pair("nextblockhash", pnext->GetBlockHash().GetHex()));

    result.push_back(json_spirit::Pair("moneysupply",ValueFromAmount(blockindex->nMoneySupply)));

    return result;
}


json_spirit::Object blockHeaderToJSON(const CBlock& block, const CBlockIndex* blockindex)
{
    json_spirit::Object result;
    result.push_back(json_spirit::Pair("version", block.nVersion));
    if (blockindex->pprev)
        result.push_back(json_spirit::Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    result.push_back(json_spirit::Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    result.push_back(json_spirit::Pair("time", block.GetBlockTime()));
    result.push_back(json_spirit::Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(json_spirit::Pair("nonce", (uint64_t)block.nNonce));
    return result;
}