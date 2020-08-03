/* @flow */
// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "kernel.h"

#include <primitives/transaction.h>
#include <primitives/block.h>
#include "blockmap.h"
#include "BlockDiskAccessor.h"
#include "chain.h"
#include "chainparams.h"
#include "script/interpreter.h"
#include "script/SignatureCheckers.h"
#include "script/standard.h"
#include <streams.h>
#include "utilstrencodings.h"

#include <boost/assign/list_of.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>

extern const int nHashDrift = 45;

extern BlockMap mapBlockIndex;
extern CChain chainActive;

static constexpr unsigned int MAXIMUM_COIN_AGE_WEIGHT_FOR_STAKING = 60 * 60 * 24 * 7 - 60 * 60;

// MODIFIER_INTERVAL: time to elapse before new modifier is computed
static const int MODIFIER_INTERVAL_RATIO = 3;
// MODIFIER_INTERVAL_RATIO:
// ratio of group interval length between the last group and the first group
static const unsigned int MODIFIER_INTERVAL = 60;

// Modifier interval: time to elapse before new modifier is computed
// Set to 3-hour for production network and 20-minute for test network
unsigned int nModifierInterval;
int nStakeTargetSpacing = 60;

// Hard checkpoints of stake modifiers to ensure they are deterministic
static std::map<int, unsigned int> mapStakeModifierCheckpoints =
        boost::assign::map_list_of(0, 0xfd11f4e7u);

// Get the last stake modifier and its generation time from a given block
static bool GetLastStakeModifier(const CBlockIndex* pindex, uint64_t& nStakeModifier, int64_t& nModifierTime)
{
    if (!pindex)
        return error("GetLastStakeModifier: null pindex");
    while (pindex && pindex->pprev && !pindex->GeneratedStakeModifier())
        pindex = pindex->pprev;
    if (!pindex->GeneratedStakeModifier())
        return error("GetLastStakeModifier: no generation at genesis block");
    nStakeModifier = pindex->nStakeModifier;
    nModifierTime = pindex->GetBlockTime();
    return true;
}

// Get selection interval section (in seconds)
static int64_t GetStakeModifierSelectionIntervalSection(int nSection)
{
    assert(nSection >= 0 && nSection < 64);
    int64_t a = MODIFIER_INTERVAL * 63 / (63 + ((63 - nSection) * (MODIFIER_INTERVAL_RATIO - 1)));
    return a;
}

// Get stake modifier selection interval (in seconds)
static int64_t GetStakeModifierSelectionInterval()
{
    int64_t nSelectionInterval = 0;
    for (int nSection = 0; nSection < 64; nSection++) {
        nSelectionInterval += GetStakeModifierSelectionIntervalSection(nSection);
    }
    return nSelectionInterval;
}

// select a block from the candidate blocks in vSortedByTimestamp, excluding
// already selected blocks in vSelectedBlocks, and with timestamp up to
// nSelectionIntervalStop.
static bool SelectBlockFromCandidates(
        std::vector<std::pair<int64_t, uint256> >& vSortedByTimestamp,
        std::map<uint256, const CBlockIndex*>& mapSelectedBlocks,
        int64_t nSelectionIntervalStop,
        uint64_t nStakeModifierPrev,
        const CBlockIndex** pindexSelected)
{
    bool fModifierV2 = false;
    bool fFirstRun = true;
    bool fSelected = false;
    uint256 hashBest = 0;
    *pindexSelected = (const CBlockIndex*)0;
    BOOST_FOREACH (const PAIRTYPE(int64_t, uint256) & item, vSortedByTimestamp) {
        if (!mapBlockIndex.count(item.second))
            return error("SelectBlockFromCandidates: failed to find block index for candidate block %s", item.second.ToString().c_str());

        const CBlockIndex* pindex = mapBlockIndex[item.second];
        if (fSelected && pindex->GetBlockTime() > nSelectionIntervalStop)
            break;

        //if the lowest block height (vSortedByTimestamp[0]) is >= switch height, use new modifier calc
        if (fFirstRun){
            fModifierV2 = false;
            fFirstRun = false;
        }

        if (mapSelectedBlocks.count(pindex->GetBlockHash()) > 0)
            continue;

        // compute the selection hash by hashing an input that is unique to that block
        uint256 hashProof;
        if(fModifierV2)
            hashProof = pindex->GetBlockHash();
        else
            hashProof = pindex->IsProofOfStake() ? 0 : pindex->GetBlockHash();

        CDataStream ss(SER_GETHASH, 0);
        ss << hashProof << nStakeModifierPrev;
        uint256 hashSelection = Hash(ss.begin(), ss.end());

        // the selection hash is divided by 2**32 so that proof-of-stake block
        // is always favored over proof-of-work block. this is to preserve
        // the energy efficiency property
        if (pindex->IsProofOfStake())
            hashSelection >>= 32;

        if (fSelected && hashSelection < hashBest) {
            hashBest = hashSelection;
            *pindexSelected = (const CBlockIndex*)pindex;
        } else if (!fSelected) {
            fSelected = true;
            hashBest = hashSelection;
            *pindexSelected = (const CBlockIndex*)pindex;
        }
    }
    if (GetBoolArg("-printstakemodifier", false))
        LogPrintf("SelectBlockFromCandidates: selection hash=%s\n", hashBest.ToString().c_str());
    return fSelected;
}

// Stake Modifier (hash modifier of proof-of-stake):
// The purpose of stake modifier is to prevent a txout (coin) owner from
// computing future proof-of-stake generated by this txout at the time
// of transaction confirmation. To meet kernel protocol, the txout
// must hash with a future stake modifier to generate the proof.
// Stake modifier consists of bits each of which is contributed from a
// selected block of a given block group in the past.
// The selection of a block is based on a hash of the block's proof-hash and
// the previous stake modifier.
// Stake modifier is recomputed at a fixed time interval instead of every
// block. This is to make it difficult for an attacker to gain control of
// additional bits in the stake modifier, even after generating a chain of
// blocks.
bool ComputeNextStakeModifier(const CBlockIndex* pindexPrev, uint64_t& nStakeModifier, bool& fGeneratedStakeModifier)
{
    nStakeModifier = 0;
    fGeneratedStakeModifier = false;
    if (!pindexPrev) {
        fGeneratedStakeModifier = true;
        return true; // genesis block's modifier is 0
    }
    if (pindexPrev->nHeight == 0) {
        //Give a stake modifier to the first block
        fGeneratedStakeModifier = true;
        nStakeModifier = uint64_t("stakemodifier");
        return true;
    }

    // First find current stake modifier and its generation block time
    // if it's not old enough, return the same stake modifier
    int64_t nModifierTime = 0;
    if (!GetLastStakeModifier(pindexPrev, nStakeModifier, nModifierTime))
        return error("ComputeNextStakeModifier: unable to get last modifier");

    if (GetBoolArg("-printstakemodifier", false))
        LogPrintf("ComputeNextStakeModifier: prev modifier= %s time=%s\n", boost::lexical_cast<std::string>(nStakeModifier).c_str(), DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nModifierTime).c_str());

    if (nModifierTime / MODIFIER_INTERVAL >= pindexPrev->GetBlockTime() / MODIFIER_INTERVAL)
        return true;

    // Sort candidate blocks by timestamp
    std::vector<std::pair<int64_t, uint256> > vSortedByTimestamp;
    vSortedByTimestamp.reserve(64 * MODIFIER_INTERVAL / nStakeTargetSpacing);
    int64_t nSelectionInterval = GetStakeModifierSelectionInterval();
    int64_t nSelectionIntervalStart = (pindexPrev->GetBlockTime() / MODIFIER_INTERVAL) * MODIFIER_INTERVAL - nSelectionInterval;
    const CBlockIndex* pindex = pindexPrev;

    while (pindex && pindex->GetBlockTime() >= nSelectionIntervalStart) {
        vSortedByTimestamp.push_back(std::make_pair(pindex->GetBlockTime(), pindex->GetBlockHash()));
        pindex = pindex->pprev;
    }

    int nHeightFirstCandidate = pindex ? (pindex->nHeight + 1) : 0;
    std::reverse(vSortedByTimestamp.begin(), vSortedByTimestamp.end());
    std::sort(vSortedByTimestamp.begin(), vSortedByTimestamp.end());

    // Select 64 blocks from candidate blocks to generate stake modifier
    uint64_t nStakeModifierNew = 0;
    int64_t nSelectionIntervalStop = nSelectionIntervalStart;
    std::map<uint256, const CBlockIndex*> mapSelectedBlocks;
    for (int nRound = 0; nRound < std::min(64, (int)vSortedByTimestamp.size()); nRound++) {
        // add an interval section to the current selection round
        nSelectionIntervalStop += GetStakeModifierSelectionIntervalSection(nRound);

        // select a block from the candidates of current round
        if (!SelectBlockFromCandidates(vSortedByTimestamp, mapSelectedBlocks, nSelectionIntervalStop, nStakeModifier, &pindex))
            return error("ComputeNextStakeModifier: unable to select block at round %d", nRound);

        // write the entropy bit of the selected block
        nStakeModifierNew |= (((uint64_t)pindex->GetStakeEntropyBit()) << nRound);

        // add the selected block from candidates to selected list
        mapSelectedBlocks.insert(std::make_pair(pindex->GetBlockHash(), pindex));
        if (fDebug || GetBoolArg("-printstakemodifier", false))
            LogPrintf("ComputeNextStakeModifier: selected round %d stop=%s height=%d bit=%d\n",
                      nRound, DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nSelectionIntervalStop).c_str(), pindex->nHeight, pindex->GetStakeEntropyBit());
    }

    // Print selection map for visualization of the selected blocks
    if (fDebug || GetBoolArg("-printstakemodifier", false)) {
        std::string strSelectionMap = "";
        // '-' indicates proof-of-work blocks not selected
        strSelectionMap.insert(0, pindexPrev->nHeight - nHeightFirstCandidate + 1, '-');
        pindex = pindexPrev;
        while (pindex && pindex->nHeight >= nHeightFirstCandidate) {
            // '=' indicates proof-of-stake blocks not selected
            if (pindex->IsProofOfStake())
                strSelectionMap.replace(pindex->nHeight - nHeightFirstCandidate, 1, "=");
            pindex = pindex->pprev;
        }
        BOOST_FOREACH (const PAIRTYPE(uint256, const CBlockIndex*) & item, mapSelectedBlocks) {
            // 'S' indicates selected proof-of-stake blocks
            // 'W' indicates selected proof-of-work blocks
            strSelectionMap.replace(item.second->nHeight - nHeightFirstCandidate, 1, item.second->IsProofOfStake() ? "S" : "W");
        }
        LogPrintf("ComputeNextStakeModifier: selection height [%d, %d] map %s\n", nHeightFirstCandidate, pindexPrev->nHeight, strSelectionMap.c_str());
    }
    if (fDebug || GetBoolArg("-printstakemodifier", false)) {
        LogPrintf("ComputeNextStakeModifier: new modifier=%s time=%s\n", boost::lexical_cast<std::string>(nStakeModifierNew).c_str(), DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pindexPrev->GetBlockTime()).c_str());
    }

    nStakeModifier = nStakeModifierNew;
    fGeneratedStakeModifier = true;
    return true;
}

// The stake modifier used to hash for a stake kernel is chosen as the stake
// modifier about a selection interval later than the coin generating the kernel
bool GetKernelStakeModifier(uint256 hashBlockFrom, uint64_t& nStakeModifier, int& nStakeModifierHeight, int64_t& nStakeModifierTime, bool fPrintProofOfStake)
{
    nStakeModifier = 0;
    if (!mapBlockIndex.count(hashBlockFrom))
        return error("GetKernelStakeModifier() : block not indexed");
    const CBlockIndex* pindexFrom = mapBlockIndex[hashBlockFrom];
    nStakeModifierHeight = pindexFrom->nHeight;
    nStakeModifierTime = pindexFrom->GetBlockTime();
    int64_t nStakeModifierSelectionInterval = GetStakeModifierSelectionInterval();
    const CBlockIndex* pindex = pindexFrom;
    CBlockIndex* pindexNext = chainActive[pindexFrom->nHeight + 1];

    // loop to find the stake modifier later by a selection interval
    while (nStakeModifierTime < pindexFrom->GetBlockTime() + nStakeModifierSelectionInterval) {
        if (!pindexNext) {
            // Should never happen
            nStakeModifierHeight = pindexFrom->nHeight;
            nStakeModifierTime = pindexFrom->GetBlockTime();
            if(pindex->GeneratedStakeModifier())
                nStakeModifier = pindex->nStakeModifier;
            return true;
        }

        pindex = pindexNext;
        pindexNext = chainActive[pindexNext->nHeight + 1];
        if (pindex->GeneratedStakeModifier()) {
            nStakeModifierHeight = pindex->nHeight;
            nStakeModifierTime = pindex->GetBlockTime();
        }
    }
    nStakeModifier = pindex->nStakeModifier;
    return true;
}

uint256 stakeHash(uint64_t stakeModifier, unsigned int nTimeTx, const COutPoint& prevout, unsigned int nTimeBlockFrom)
{
    //Divi will hash in the transaction hash and the index number in order to make sure each hash is unique
    CDataStream ss(SER_GETHASH, 0);
    ss << stakeModifier << nTimeBlockFrom << prevout.n << prevout.hash << nTimeTx;
    return Hash(ss.begin(), ss.end());
}

//test hash vs target
bool stakeTargetHit(const uint256& hashProofOfStake, int64_t nValueIn, const uint256& bnTargetPerCoinDay, int64_t nTimeWeight)
{
    const uint256 bnCoinDayWeight = uint256(nValueIn) * nTimeWeight / COIN / 400;

    uint256 target = bnTargetPerCoinDay;
    if (!target.MultiplyBy(bnCoinDayWeight)) {
        // In regtest with minimal difficulty, it may happen that the
        // modification overflows the uint256, in which case it just means
        // that the target will always be hit.
        return true;
    }

    // Now check if proof-of-stake hash meets target protocol
    return hashProofOfStake < target;
}


ProofOfStakeCalculator::ProofOfStakeCalculator(
    const COutPoint& utxoToStake,
    const int64_t& utxoValue,
    const uint64_t& stakeModifier,
    unsigned int blockDifficultyBits
    ): utxoToStake_(utxoToStake)
    , utxoValue_(utxoValue)
    , stakeModifier_(stakeModifier)
    , targetPerCoinDay_(uint256().SetCompact(blockDifficultyBits))
{
}

bool ProofOfStakeCalculator::computeProofOfStakeAndCheckItMeetsTarget(
    unsigned int nTimeTx,
    unsigned int nTimeBlockFrom,
    uint256& computedProofOfStake,
    bool checkOnly) const
{
    if(!checkOnly) computedProofOfStake = stakeHash(stakeModifier_,nTimeTx, utxoToStake_,nTimeBlockFrom);
    auto&& coinAgeWeightOfUtxo = std::min<int64_t>(nTimeTx - nTimeBlockFrom, MAXIMUM_COIN_AGE_WEIGHT_FOR_STAKING);
    return stakeTargetHit(computedProofOfStake,utxoValue_,targetPerCoinDay_, coinAgeWeightOfUtxo);
}

class LegacyProofOfStakeCalculator: public I_ProofOfStakeCalculator
{
private:
    const COutPoint& utxoToStake_;
    const int64_t& utxoValue_;
    const uint64_t& stakeModifier_;
    const uint256 targetPerCoinDay_;
    const int64_t coinAgeWeight_;
public:
    LegacyProofOfStakeCalculator(
        const COutPoint& utxoToStake,
        const int64_t& utxoValue,
        const uint64_t& stakeModifier,
        unsigned int blockDifficultyBits,
        int64_t coinAgeWeight);

    virtual bool computeProofOfStakeAndCheckItMeetsTarget(
        unsigned int nTimeTx,
        unsigned int nTimeBlockFrom,
        uint256& computedProofOfStake,
        bool checkOnly = false) const;
};

LegacyProofOfStakeCalculator::LegacyProofOfStakeCalculator(
    const COutPoint& utxoToStake,
    const int64_t& utxoValue,
    const uint64_t& stakeModifier,
    unsigned int blockDifficultyBits,
    int64_t coinAgeWeight
    ): utxoToStake_(utxoToStake)
    , utxoValue_(utxoValue)
    , stakeModifier_(stakeModifier)
    , targetPerCoinDay_(uint256().SetCompact(blockDifficultyBits))
    , coinAgeWeight_(coinAgeWeight)
{
}

bool LegacyProofOfStakeCalculator::computeProofOfStakeAndCheckItMeetsTarget(
    unsigned int nTimeTx,
    unsigned int nTimeBlockFrom,
    uint256& computedProofOfStake,
    bool checkOnly) const
{
    if(!checkOnly) computedProofOfStake = stakeHash(stakeModifier_,nTimeTx, utxoToStake_,nTimeBlockFrom);
    return stakeTargetHit(computedProofOfStake,utxoValue_,targetPerCoinDay_, coinAgeWeight_);
}

std::shared_ptr<I_ProofOfStakeCalculator> createProofOfStakeCalculator(
    I_PoSStakeModifierService& stakeModifierService,
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps,
    unsigned int nBits,
    const CBlock& blockFrom,
    const COutPoint& prevout,
    const CAmount& utxoValue,
    unsigned int& nTimeTx)
{
    //assign new variables to make it easier to read
    int64_t nValueIn = utxoValue;
    unsigned int nTimeBlockFrom = blockFrom.GetBlockTime();

    if (nTimeTx < nTimeBlockFrom) // Transaction timestamp violation
    {
        error("CreateHashProofForProofOfStake() : nTime violation");
        return std::shared_ptr<I_ProofOfStakeCalculator>();
    }

    if (nTimeBlockFrom + Params().GetMinCoinAgeForStaking() > nTimeTx) // Min age requirement
    {
        error("CreateHashProofForProofOfStake() : min age violation - nTimeBlockFrom=%d minimum coinage=%d nTimeTx=%d", nTimeBlockFrom, Params().GetMinCoinAgeForStaking(), nTimeTx);
        return std::shared_ptr<I_ProofOfStakeCalculator>();
    }

    //grab stake modifier
    auto stakeModifierData = stakeModifierService.getStakeModifier(blockFrom.GetHash());
    if (!stakeModifierData.second) {
        LogPrintf("CreateHashProofForProofOfStake(): failed to get kernel stake modifier \n");
        return std::shared_ptr<I_ProofOfStakeCalculator>();
    }

    int64_t nTimeWeight = std::min<int64_t>(nTimeTx - nTimeBlockFrom, MAXIMUM_COIN_AGE_WEIGHT_FOR_STAKING);

    return std::make_shared<LegacyProofOfStakeCalculator>(prevout,utxoValue,stakeModifierData.first,nBits,nTimeWeight);
}

std::pair<uint64_t,bool> LegacyPoSStakeModifierService::getStakeModifier(const uint256& blockHash) const
{
    uint64_t nStakeModifier = 0;
    int nStakeModifierHeight = 0;
    int64_t nStakeModifierTime = 0;
    if (!GetKernelStakeModifier(blockHash, nStakeModifier, nStakeModifierHeight, nStakeModifierTime, false)) {
        LogPrintf("CreateHashProofForProofOfStake(): failed to get kernel stake modifier \n");
        return std::make_pair(nStakeModifier,false);
    }
    return std::make_pair(nStakeModifier,true);
}

//instead of looping outside and reinitializing variables many times, we will give a nTimeTx and also search interval so that we can do all the hashing here
bool CreateHashProofForProofOfStake(
    I_PoSStakeModifierService& stakeModifierService,
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps,
    unsigned int nBits,
    const CBlock& blockFrom,
    const COutPoint& prevout,
    const CAmount& utxoValue,
    unsigned int& nTimeTx,
    bool fCheck,
    uint256& hashProofOfStake)
{
    unsigned int nTimeBlockFrom = blockFrom.GetBlockTime();
    std::shared_ptr<I_ProofOfStakeCalculator> calculator =
        createProofOfStakeCalculator(
            stakeModifierService,
            hashedBlockTimestamps,
            nBits,
            blockFrom,
            prevout,
            utxoValue,
            nTimeTx);

    if(!calculator.get())
        return false;

    //if wallet is simply checking to make sure a hash is valid
    if (fCheck) {
        return calculator->computeProofOfStakeAndCheckItMeetsTarget(nTimeTx,nTimeBlockFrom,hashProofOfStake);
    }

    bool fSuccess = false;
    int nHeightStart = chainActive.Height();
    for (unsigned int i = 0; i < nHashDrift; i++) //iterate the hashing
    {
        if (chainActive.Height() != nHeightStart)
            break;

        if(!calculator->computeProofOfStakeAndCheckItMeetsTarget(nTimeTx,nTimeBlockFrom,hashProofOfStake))
        {
            --nTimeTx;
            continue;
        }

        fSuccess = true;
        break;
    }

    hashedBlockTimestamps.clear();
    hashedBlockTimestamps[chainActive.Tip()->nHeight] = GetTime(); //store a time stamp of when we last hashed on this block
    return fSuccess;
}

bool CreateHashProofForProofOfStake(
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps,
    unsigned int nBits,
    const CBlock& blockFrom,
    const COutPoint& prevout,
    const CAmount& utxoValue,
    unsigned int& nTimeTx,
    bool fCheck,
    uint256& hashProofOfStake)
{
    static LegacyPoSStakeModifierService stakeModifierService;
    return CreateHashProofForProofOfStake(
        stakeModifierService,
        hashedBlockTimestamps,
        nBits,
        blockFrom,
        prevout,
        utxoValue,
        nTimeTx,
        fCheck,
        hashProofOfStake);
}

// Check kernel hash target and coinstake signature
bool CheckProofOfStake(const CBlock& block, uint256& hashProofOfStake)
{
    const CTransaction tx = block.vtx[1];
    if (!tx.IsCoinStake())
        return error("CheckProofOfStake() : called on non-coinstake %s", tx.GetHash().ToString().c_str());

    if(tx.vin.size() > MAX_KERNEL_COMBINED_INPUTS) {
        return error("CheckProofOfStake() : invalid amount of stake inputs, current: %d, max: %d", tx.vin.size(), MAX_KERNEL_COMBINED_INPUTS);
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
        if (!GetTransaction(tx.vin[i].prevout.hash, txPrev2, hashBlock2))
            return error("CheckProofOfStake() : INFO: read txPrev failed for input %u", i);
        if (txPrev2.vout[tx.vin[i].prevout.n].scriptPubKey != kernelScript)
            return error("CheckProofOfStake() : Stake input %u pays to different script", i);
    }

    //verify signature and script
    if (!VerifyScript(txin.scriptSig, txPrev.vout[txin.prevout.n].scriptPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&tx, 0)))
        return error("CheckProofOfStake() : VerifySignature failed on coinstake %s", tx.GetHash().ToString().c_str());

    CBlockIndex* pindex = NULL;
    BlockMap::iterator it = mapBlockIndex.find(hashBlock);
    if (it != mapBlockIndex.end())
        pindex = it->second;
    else
        return error("CheckProofOfStake() : read block failed");

    // Read block header
    CBlock blockprev;
    if (!ReadBlockFromDisk(blockprev, pindex->GetBlockPos()))
        return error("CheckProofOfStake(): INFO: failed to find block");

    unsigned int nInterval = 0;
    unsigned int nTime = block.nTime;
    std::map<unsigned int, unsigned int> hashedBlockTimestamps;
    if (!CreateHashProofForProofOfStake(hashedBlockTimestamps, block.nBits, blockprev, txin.prevout, txPrev.vout[txin.prevout.n].nValue, nTime, true, hashProofOfStake))
        return error("CheckProofOfStake() : INFO: check kernel failed on coinstake %s, hashProof=%s \n", tx.GetHash().ToString().c_str(), hashProofOfStake.ToString().c_str()); // may occur during initial download or if behind on block chain sync

    return true;
}

// Get stake modifier checksum
unsigned int GetStakeModifierChecksum(const CBlockIndex* pindex)
{
    assert(pindex->pprev || pindex->GetBlockHash() == Params().HashGenesisBlock());
    // Hash previous checksum with flags, hashProofOfStake and nStakeModifier
    CDataStream ss(SER_GETHASH, 0);
    if (pindex->pprev)
        ss << pindex->pprev->nStakeModifierChecksum;
    ss << pindex->nFlags << pindex->hashProofOfStake << pindex->nStakeModifier;
    uint256 hashChecksum = Hash(ss.begin(), ss.end());
    hashChecksum >>= (256 - 32);
    return hashChecksum.Get64();
}

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(int nHeight, unsigned int nStakeModifierChecksum)
{
    if (mapStakeModifierCheckpoints.count(nHeight)) {
        return nStakeModifierChecksum == mapStakeModifierCheckpoints[nHeight];
    }
    return true;
}

void SetStakeModifiersForNewBlockIndex(CBlockIndex* pindexNew)
{
    uint64_t nStakeModifier = 0;
    bool fGeneratedStakeModifier = false;
    if (!ComputeNextStakeModifier(pindexNew->pprev, nStakeModifier, fGeneratedStakeModifier))
        LogPrintf("AddToBlockIndex() : ComputeNextStakeModifier() failed \n");
    pindexNew->SetStakeModifier(nStakeModifier, fGeneratedStakeModifier);
    pindexNew->nStakeModifierChecksum = GetStakeModifierChecksum(pindexNew);
    if (!CheckStakeModifierCheckpoints(pindexNew->nHeight, pindexNew->nStakeModifierChecksum))
        LogPrintf("AddToBlockIndex() : Rejected by stake modifier checkpoint height=%d, modifier=%s \n", pindexNew->nHeight, boost::lexical_cast<std::string>(nStakeModifier));
}
