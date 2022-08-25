#include <BlockCheckingHelpers.h>


#include <primitives/block.h>
#include <chainparams.h>
#include <Logging.h>
#include <ValidationState.h>
#include <Settings.h>
#include <pow.h>
#include <utiltime.h>
#include <defaultValues.h>
#include <version.h>
#include <TransactionOpCounting.h>
#include <utilmoneystr.h>
#include <timedata.h>

extern Settings& settings;

bool CheckBlock(const CBlock& block, CValidationState& state)
{
    // These are checks that are independent of context.

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (block.IsProofOfWork() && !CheckProofOfWork(block.GetHash(), block.nBits, Params()))
    {
        return state.DoS(150, error("%s : proof of work failed",__func__),
                         REJECT_INVALID, "bad-header: high-hash", true);
    }

    // Check timestamp
    LogPrint("debug", "%s: block=%s  is proof of stake=%d\n", __func__, block.GetHash(), block.IsProofOfStake());
    if (block.GetBlockTime() > GetAdjustedTime() + (block.IsProofOfStake() ? settings.MaxFutureBlockDrift() : 7200)) // 3 minute future drift for PoS
        return state.Invalid(error("%s : block timestamp too far in the future",__func__),
                             REJECT_INVALID, "time-too-new");

    // Check the merkle root.
    bool mutated;
    uint256 hashMerkleRoot2 = block.BuildMerkleTree(&mutated);
    if (block.hashMerkleRoot != hashMerkleRoot2)
        return state.DoS(100, error("%s : hashMerkleRoot mismatch",__func__),
                         REJECT_INVALID, "bad-txnmrklroot", true);

    // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
    // of transactions in a block without affecting the merkle root of a block,
    // while still invalidating it.
    if (mutated)
        return state.DoS(100, error("%s : duplicate transaction",__func__),
                         REJECT_INVALID, "bad-txns-duplicate", true);

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.

    // Size limits
    if (block.vtx.empty() || block.vtx.size() > MAX_BLOCK_SIZE_CURRENT || ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE_CURRENT)
        return state.DoS(100, error("%s : size limits failed",__func__),
                         REJECT_INVALID, "bad-blk-length");

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0].IsCoinBase())
        return state.DoS(100, error("%s : first tx is not coinbase",__func__),
                         REJECT_INVALID, "bad-cb-missing");
    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i].IsCoinBase())
            return state.DoS(100, error("%s : more than one coinbase",__func__),
                             REJECT_INVALID, "bad-cb-multiple");

    if (block.IsProofOfStake()) {
        // Coinbase output should be empty if proof-of-stake block
        if (block.vtx[0].vout.size() != 1 || !block.vtx[0].vout[0].IsEmpty())
            return state.DoS(100, error("%s : coinbase output not empty for proof-of-stake block",__func__));

        // Second transaction must be coinstake, the rest must not be
        if (block.vtx.empty() || !block.vtx[1].IsCoinStake())
            return state.DoS(100, error("%s : second tx is not coinstake",__func__));
        for (unsigned int i = 2; i < block.vtx.size(); i++)
            if (block.vtx[i].IsCoinStake())
                return state.DoS(100, error("%s : more than one coinstake",__func__));
    }

    // Check transactions
    std::set<COutPoint> inputsUsedByBlockTransactions;
    for (const CTransaction& tx : block.vtx) {
        if (!CheckTransaction(tx, state,inputsUsedByBlockTransactions))
            return error("%s : CheckTransaction failed",__func__);
    }


    unsigned int nSigOps = 0;
    for(const CTransaction& tx: block.vtx) {
        nSigOps += GetLegacySigOpCount(tx);
    }
    unsigned int nMaxBlockSigOps = MAX_BLOCK_SIGOPS_LEGACY;
    if (nSigOps > nMaxBlockSigOps)
        return state.DoS(100, error("%s : out-of-bounds SigOpCount",__func__),
                         REJECT_INVALID, "bad-blk-sigops", true);

    return true;
}

bool CheckTransaction(const CTransaction& tx, CValidationState& state, std::set<COutPoint>& usedInputsSet)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.DoS(10, error("%s : vin empty",__func__),
                         REJECT_INVALID, "bad-txns-vin-empty");
    if (tx.vout.empty())
        return state.DoS(10, error("%s : vout empty",__func__),
                         REJECT_INVALID, "bad-txns-vout-empty");

    // Size limits
    unsigned int nMaxSize = MAX_STANDARD_TX_SIZE;

    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > nMaxSize)
        return state.DoS(100, error("%s : size limits failed",__func__),
                         REJECT_INVALID, "bad-txns-oversize");

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    const CAmount maxMoneyAllowedInOutput = Params().MaxMoneyOut();
    for(const CTxOut& txout: tx.vout)
    {
        if (txout.IsEmpty() && !tx.IsCoinBase() && !tx.IsCoinStake())
            return state.DoS(100, error("%s: txout empty for user transaction",__func__));
        if(!MoneyRange(txout.nValue,maxMoneyAllowedInOutput))
            return state.DoS(100, error("%s : txout.nValue out of range",__func__),
                             REJECT_INVALID, "bad-txns-vout-negative-or-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut,maxMoneyAllowedInOutput))
            return state.DoS(100, error("%s : txout total out of range",__func__),
                             REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    // Check for duplicate inputs
    for (const CTxIn& txin : tx.vin) {
        if (usedInputsSet.count(txin.prevout))
            return state.DoS(100, error("%s : duplicate inputs",__func__),
                             REJECT_INVALID, "bad-txns-inputs-duplicate");

        usedInputsSet.insert(txin.prevout);
    }

    if (tx.IsCoinBase()) {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 150)
            return state.DoS(100, error("%s : coinbase script size=%d",__func__, tx.vin[0].scriptSig.size()),
                    REJECT_INVALID, "bad-cb-length");
    }

    return true;
}
bool CheckTransaction(const CTransaction& tx, CValidationState& state)
{
    std::set<COutPoint> vInOutPoints;
    return CheckTransaction(tx,state,vInOutPoints);
}