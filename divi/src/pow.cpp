// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "chain.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "uint256.h"
#include <Logging.h>

#include <math.h>

#include <I_ProofOfStakeGenerator.h>
#include <Settings.h>
#include <StakingData.h>
#include <BlockDiskAccessor.h>
#include <TransactionDiskAccessor.h>
#include <script/SignatureCheckers.h>
#include <blockmap.h>
#include <script/standard.h>
#include <I_DifficultyAdjuster.h>

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const CChainParams& chainParameters)
{
    bool fNegative;
    bool fOverflow;
    uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > chainParameters.ProofOfWorkLimit())
        return error("CheckProofOfWork() : nBits below minimum work");

    // Check proof of work matches claimed amount
    if (hash > bnTarget)
        return error("CheckProofOfWork() : hash doesn't match nBits");

    return true;
}

// Check kernel hash target and coinstake signature
bool CheckProofOfStakeContextAndRecoverStakingData(
    const Settings& settings,
    const BlockMap& blockIndicesByHash,
    const CBlock& block,
    const CBlockIndex* pindexPrev,
    StakingData& stakingData)
{
    static const unsigned maxInputs = settings.MaxNumberOfPoSCombinableInputs();
    const CTransaction tx = block.vtx[1];
    if (!tx.IsCoinStake())
        return error("%s : called on non-coinstake %s", __func__, tx.ToStringShort());

    if(tx.vin.size() > maxInputs) {
        return error("%s : invalid amount of stake inputs, current: %d, max: %d", __func__, tx.vin.size(), maxInputs);
    }

    // Kernel (input 0) must match the stake hash target per coin age (nBits)
    const CTxIn& txin = tx.vin[0];

    // First try finding the previous transaction in database
    uint256 hashBlock;
    CTransaction txPrev;
    if (!GetTransaction(txin.prevout.hash, txPrev, hashBlock, true))
        return error("%s : INFO: read txPrev failed", __func__);

    const CScript &kernelScript = txPrev.vout[txin.prevout.n].scriptPubKey;

    // All other inputs (if any) must pay to the same script.
    for (unsigned i = 1; i < tx.vin.size (); ++i) {
        CTransaction txPrev2;
        uint256 hashBlock2;
        if (!GetTransaction(tx.vin[i].prevout.hash, txPrev2, hashBlock2, true))
            return error("%s : INFO: read txPrev failed for input %u",__func__, i);
        if (txPrev2.vout[tx.vin[i].prevout.n].scriptPubKey != kernelScript)
            return error("%s : Stake input %u pays to different script", __func__, i);
    }

    //verify signature and script
    if (!VerifyScript(txin.scriptSig, txPrev.vout[txin.prevout.n], POS_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&tx, 0)))
        return error("%s : VerifySignature failed on coinstake %s", __func__, tx.ToStringShort());

    CBlockIndex* pindex = NULL;
    BlockMap::const_iterator it = blockIndicesByHash.find(hashBlock);
    if (it != blockIndicesByHash.end())
        pindex = it->second;
    else
        return error("%s : read block failed",__func__);

    // Read block header
    CBlock blockprev;
    if (!ReadBlockFromDisk(blockprev, pindex->GetBlockPos()))
        return error("%s : INFO: failed to find block",__func__);

    stakingData = StakingData(
        block.nBits,
        blockprev.GetBlockTime(),
        blockprev.GetHash(),
        txin.prevout,
        txPrev.vout[txin.prevout.n].nValue,
        pindexPrev->GetBlockHash());

    return true;
}
bool CheckProofOfStake(
    const I_ProofOfStakeGenerator& posGenerator,
    const Settings& settings,
    const BlockMap& blockIndicesByHash,
    const CBlock& block,
    const CBlockIndex* pindexPrev,
    uint256& hashProofOfStake)
{
    StakingData stakingData;
    if(!CheckProofOfStakeContextAndRecoverStakingData(settings,blockIndicesByHash,block,pindexPrev,stakingData))
        return false;
    if (!posGenerator.computeAndVerifyProofOfStake(stakingData, block.nTime, hashProofOfStake))
        return error("CheckProofOfStake() : INFO: check kernel failed on coinstake %s, hashProof=%s \n",
            block.vtx[1].ToStringShort(), hashProofOfStake); // may occur during initial download or if behind on block chain sync

    return true;
}

double ConvertBitsToDouble(unsigned int nBits)
{
    int nShift = (nBits >> 24) & 0xff;

    double dDiff =
            (double)0x0000ffff / (double)(nBits & 0x00ffffff);

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

bool CheckWork(
    const CChainParams& chainParameters,
    const I_DifficultyAdjuster& difficultyAdjuster,
    const I_ProofOfStakeGenerator& posGenerator,
    const BlockMap& blockIndicesByHash,
    const Settings& settings,
    const CBlock& block,
    uint256& hashProofOfStake,
    CBlockIndex* const pindexPrev)
{
    if (pindexPrev == NULL)
        return error("%s : null pindexPrev for block %s", __func__, block.GetHash());

    const unsigned int nBitsRequired = difficultyAdjuster.computeNextBlockDifficulty(pindexPrev);

    if(block.IsProofOfWork()) return CheckProofOfWork(block.GetHash(),block.nBits,chainParameters) && block.nBits == nBitsRequired;

    if (block.IsProofOfStake()) {
        hashProofOfStake = uint256(0);
        uint256 hash = block.GetHash();

        if(!CheckProofOfStake(posGenerator,settings,blockIndicesByHash,block,pindexPrev, hashProofOfStake)) {
            return error("%s check proof-of-stake failed for block %s\n",__func__, hash);
        }
        return true;
    }

    return false;
}