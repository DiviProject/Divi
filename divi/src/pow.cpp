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

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CChainParams& chainParameters)
{
    /* current difficulty formula, divi - DarkGravity v3, written by Evan Duffield - evan@dashpay.io */
    const CBlockIndex* BlockLastSolved = pindexLast;
    const CBlockIndex* BlockReading = pindexLast;
    int64_t nActualTimespan = 0;
    int64_t LastBlockTime = 0;
    int64_t PastBlocksMin = 24;
    int64_t PastBlocksMax = 24;
    int64_t CountBlocks = 0;
    uint256 PastDifficultyAverage;
    uint256 PastDifficultyAveragePrev;

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || BlockLastSolved->nHeight < PastBlocksMin) {
        return chainParameters.ProofOfWorkLimit().GetCompact();
    }

    if (!chainParameters.RetargetDifficulty())
        return BlockLastSolved->nBits;

    if (pindexLast->nHeight > chainParameters.LAST_POW_BLOCK()) {
        uint256 bnTargetLimit = (~uint256(0) >> 24);
        int64_t nTargetSpacing = 60;
        int64_t nTargetTimespan = 60 * 40;

        int64_t nActualSpacing = 0;
        if (pindexLast->nHeight != 0)
            nActualSpacing = pindexLast->GetBlockTime() - pindexLast->pprev->GetBlockTime();

        if (nActualSpacing < 0)
            nActualSpacing = 1;

        // ppcoin: target change every block
        // ppcoin: retarget with exponential moving toward target spacing
        uint256 bnNew;
        bnNew.SetCompact(pindexLast->nBits);

        int64_t nInterval = nTargetTimespan / nTargetSpacing;
        bnNew *= ((nInterval - 1) * nTargetSpacing + nActualSpacing + nActualSpacing);
        bnNew /= ((nInterval + 1) * nTargetSpacing);

        if (bnNew <= 0 || bnNew > bnTargetLimit)
            bnNew = bnTargetLimit;

        return bnNew.GetCompact();
    }

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) {
            break;
        }
        CountBlocks++;

        if (CountBlocks <= PastBlocksMin) {
            if (CountBlocks == 1) {
                PastDifficultyAverage.SetCompact(BlockReading->nBits);
            } else {
                PastDifficultyAverage = ((PastDifficultyAveragePrev * CountBlocks) + (uint256().SetCompact(BlockReading->nBits))) / (CountBlocks + 1);
            }
            PastDifficultyAveragePrev = PastDifficultyAverage;
        }

        if (LastBlockTime > 0) {
            int64_t Diff = (LastBlockTime - BlockReading->GetBlockTime());
            nActualTimespan += Diff;
        }
        LastBlockTime = BlockReading->GetBlockTime();

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    uint256 bnNew(PastDifficultyAverage);

    int64_t _nTargetTimespan = CountBlocks * chainParameters.TargetSpacing();

    if (nActualTimespan < _nTargetTimespan / 3)
        nActualTimespan = _nTargetTimespan / 3;
    if (nActualTimespan > _nTargetTimespan * 3)
        nActualTimespan = _nTargetTimespan * 3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= _nTargetTimespan;

    if (bnNew > chainParameters.ProofOfWorkLimit()) {
        bnNew = chainParameters.ProofOfWorkLimit();
    }

    return bnNew.GetCompact();
}

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

uint256 GetBlockProof(const CBlockIndex& block)
{
    uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}

// Check kernel hash target and coinstake signature
bool CheckProofOfStakeContextAndRecoverStakingData(
    const Settings& settings,
    const BlockMap& blockIndicesByHash,
    const CBlock& block,
    CBlockIndex* pindexPrev,
    StakingData& stakingData)
{
    static const unsigned maxInputs = settings.MaxNumberOfPoSCombinableInputs();
    const CTransaction tx = block.vtx[1];
    if (!tx.IsCoinStake())
        return error("CheckProofOfStake() : called on non-coinstake %s", tx.ToStringShort());

    if(tx.vin.size() > maxInputs) {
        return error("CheckProofOfStake() : invalid amount of stake inputs, current: %d, max: %d", tx.vin.size(), maxInputs);
    }

    // Kernel (input 0) must match the stake hash target per coin age (nBits)
    const CTxIn& txin = tx.vin[0];

    // First try finding the previous transaction in database
    uint256 hashBlock;
    CTransaction txPrev;
    if (!GetTransaction(txin.prevout.hash, txPrev, hashBlock, true))
        return error("CheckProofOfStake() : INFO: read txPrev failed");

    const CScript &kernelScript = txPrev.vout[txin.prevout.n].scriptPubKey;

    // All other inputs (if any) must pay to the same script.
    for (unsigned i = 1; i < tx.vin.size (); ++i) {
        CTransaction txPrev2;
        uint256 hashBlock2;
        if (!GetTransaction(tx.vin[i].prevout.hash, txPrev2, hashBlock2, true))
            return error("CheckProofOfStake() : INFO: read txPrev failed for input %u", i);
        if (txPrev2.vout[tx.vin[i].prevout.n].scriptPubKey != kernelScript)
            return error("CheckProofOfStake() : Stake input %u pays to different script", i);
    }

    //verify signature and script
    if (!VerifyScript(txin.scriptSig, txPrev.vout[txin.prevout.n], POS_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&tx, 0)))
        return error("CheckProofOfStake() : VerifySignature failed on coinstake %s", tx.ToStringShort());

    CBlockIndex* pindex = NULL;
    BlockMap::const_iterator it = blockIndicesByHash.find(hashBlock);
    if (it != blockIndicesByHash.end())
        pindex = it->second;
    else
        return error("CheckProofOfStake() : read block failed");

    // Read block header
    CBlock blockprev;
    if (!ReadBlockFromDisk(blockprev, pindex->GetBlockPos()))
        return error("CheckProofOfStake(): INFO: failed to find block");

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
    CBlockIndex* pindexPrev,
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
    const I_ProofOfStakeGenerator& posGenerator,
    const BlockMap& blockIndicesByHash,
    const Settings& settings,
    const CBlock& block,
    uint256& hashProofOfStake,
    CBlockIndex* const pindexPrev)
{
    if (pindexPrev == NULL)
        return error("%s : null pindexPrev for block %s", __func__, block.GetHash());

    unsigned int nBitsRequired = GetNextWorkRequired(pindexPrev,chainParameters);

    if (block.IsProofOfWork() && (pindexPrev->nHeight + 1 <= 68589)) {
        double n1 = ConvertBitsToDouble(block.nBits);
        double n2 = ConvertBitsToDouble(nBitsRequired);

        const double absoluteDifference = std::abs(n1 - n2);
        if (absoluteDifference > n1 * 0.5)
            return error("%s : incorrect proof of work (DGW pre-fork) - %f %f %f at %d", __func__, absoluteDifference, n1, n2, pindexPrev->nHeight + 1);

        return true;
    }

    if (block.nBits != nBitsRequired)
        return error("%s : incorrect proof of work at %d", __func__, pindexPrev->nHeight + 1);

    if (block.IsProofOfStake()) {
        hashProofOfStake = uint256(0);
        uint256 hash = block.GetHash();

        if(!CheckProofOfStake(posGenerator,settings,blockIndicesByHash,block,pindexPrev, hashProofOfStake)) {
            LogPrintf("WARNING: ProcessBlock(): check proof-of-stake failed for block %s\n", hash);
            return false;
        }
    }

    return true;
}