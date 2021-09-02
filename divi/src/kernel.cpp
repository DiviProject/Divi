/* @flow */
// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "kernel.h"

#include <uint256.h>
#include <primitives/transaction.h>
#include <primitives/block.h>
#include "blockmap.h"
#include "BlockDiskAccessor.h"
#include "BlockRewards.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include <ForkActivation.h>
#include "script/interpreter.h"
#include "script/SignatureCheckers.h"
#include "script/standard.h"
#include "script/StakingVaultScript.h"
#include <TransactionDiskAccessor.h>
#include <streams.h>
#include "utilmoneystr.h"
#include "utilstrencodings.h"

#include <boost/assign/list_of.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include <StakingData.h>
#include <StakeModifierIntervalHelpers.h>
#include <Logging.h>
#include <utiltime.h>
#include <I_ProofOfStakeGenerator.h>

#include <Settings.h>


// Hard checkpoints of stake modifiers to ensure they are deterministic
static std::map<int, unsigned int> mapStakeModifierCheckpoints =
        boost::assign::map_list_of(0, 0xfd11f4e7u);

// Get the last stake modifier and its generation time from a given block
const CBlockIndex* GetLastBlockIndexWithGeneratedStakeModifier(const CBlockIndex* pindex)
{
    while (pindex && pindex->pprev && !pindex->GeneratedStakeModifier())
    {
        pindex = pindex->pprev;
    }
    return pindex;
}

// select a block from the candidate blocks in vSortedByTimestamp, excluding
// already selected blocks in vSelectedBlocks, and with timestamp up to
// nSelectionIntervalStop.
static const CBlockIndex* SelectBlockFromCandidates(
    const BlockMap& blockIndicesByHash,
    const std::vector<std::pair<int64_t, uint256> >& vSortedByTimestamp,
    const std::map<uint256, const CBlockIndex*>& mapSelectedBlocks,
    int64_t nSelectionIntervalStop,
    uint64_t nStakeModifierPrev)
{
    bool fSelected = false;
    uint256 hashBest = 0;
    const CBlockIndex* pindexSelected = nullptr;
    for (const std::pair<int64_t, uint256>& item: vSortedByTimestamp)
    {
        if (!blockIndicesByHash.count(item.second))
        {
            error("%s: failed to find block index for candidate block %s",__func__, item.second);
            return nullptr;
        }

        const CBlockIndex* pindex = blockIndicesByHash.find(item.second)->second;
        if (fSelected && pindex->GetBlockTime() > nSelectionIntervalStop)
            break;

        if (mapSelectedBlocks.count(pindex->GetBlockHash()) > 0)
            continue;

        // compute the selection hash by hashing an input that is unique to that block
        const uint256 blockSelectionRandomnessSeed = pindex->IsProofOfStake() ? 0 : pindex->GetBlockHash();

        // the selection hash is divided by 2**32 so that proof-of-stake block
        // is always favored over proof-of-work block. this is to preserve
        // the energy efficiency property
        CDataStream ss(SER_GETHASH, 0);
        ss << blockSelectionRandomnessSeed << nStakeModifierPrev;
        const uint256 hashSelection = pindex->IsProofOfStake()? Hash(ss.begin(), ss.end()) >> 32 : Hash(ss.begin(), ss.end());

        if (fSelected && hashSelection < hashBest)
        {
            hashBest = hashSelection;
            pindexSelected = pindex;
        }
        else if (!fSelected)
        {
            fSelected = true;
            hashBest = hashSelection;
            pindexSelected = pindex;
        }
    }
    return pindexSelected;
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
std::vector<std::pair<int64_t, uint256> > GetRecentBlocksSortedByIncreasingTimestamp(
    const CBlockIndex* pindexPrev,
    int64_t& nSelectionIntervalStop)
{
    // Sort candidate blocks by timestamp
    std::vector<std::pair<int64_t, uint256> > vSortedByTimestamp;
    vSortedByTimestamp.reserve(64);
    int64_t nSelectionInterval = GetStakeModifierSelectionInterval();
    int64_t nSelectionIntervalStart = (pindexPrev->GetBlockTime() / MODIFIER_INTERVAL) * MODIFIER_INTERVAL - nSelectionInterval;
    const CBlockIndex* pindex = pindexPrev;

    while (pindex && pindex->GetBlockTime() >= nSelectionIntervalStart) {
        vSortedByTimestamp.push_back(std::make_pair(pindex->GetBlockTime(), pindex->GetBlockHash()));
        pindex = pindex->pprev;
    }

    std::reverse(vSortedByTimestamp.begin(), vSortedByTimestamp.end());
    std::sort(vSortedByTimestamp.begin(), vSortedByTimestamp.end());

    nSelectionIntervalStop = nSelectionIntervalStart;
    return vSortedByTimestamp;
}
bool ComputeNextStakeModifier(
    const BlockMap& blockIndicesByHash,
    const CBlockIndex* pindexPrev,
    uint64_t& nStakeModifier,
    bool& fGeneratedStakeModifier)
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
    const CBlockIndex* indexWhereLastStakeModifierWasSet = GetLastBlockIndexWithGeneratedStakeModifier(pindexPrev);
    if (!indexWhereLastStakeModifierWasSet || !indexWhereLastStakeModifierWasSet->GeneratedStakeModifier())
        return error("ComputeNextStakeModifier: unable to get last modifier prior to blockhash %s\n",pindexPrev->GetBlockHash());

    int64_t nModifierTime = indexWhereLastStakeModifierWasSet->GetBlockTime();
    nStakeModifier = indexWhereLastStakeModifierWasSet->nStakeModifier;

    if (nModifierTime / MODIFIER_INTERVAL >= pindexPrev->GetBlockTime() / MODIFIER_INTERVAL)
        return true;

    uint64_t nStakeModifierNew = 0;
    int64_t nSelectionIntervalStop = 0;
    std::vector<std::pair<int64_t, uint256> > vSortedByTimestamp = GetRecentBlocksSortedByIncreasingTimestamp(pindexPrev,nSelectionIntervalStop);

    std::map<uint256, const CBlockIndex*> mapSelectedBlocks;
    for (int nRound = 0; nRound < std::min(64, (int)vSortedByTimestamp.size()); nRound++) {
        nSelectionIntervalStop += GetStakeModifierSelectionIntervalSection(nRound);
        const CBlockIndex* pindex = SelectBlockFromCandidates(blockIndicesByHash,vSortedByTimestamp, mapSelectedBlocks, nSelectionIntervalStop, nStakeModifier);
        if (!pindex) return error("ComputeNextStakeModifier: unable to select block at round %d", nRound);

        nStakeModifierNew |= (((uint64_t)pindex->GetStakeEntropyBit()) << nRound);
        mapSelectedBlocks.insert(std::make_pair(pindex->GetBlockHash(), pindex));
    }

    if(ActivationState(pindexPrev).IsActive(Fork::HardenedStakeModifier))
    {
        CHashWriter hasher(SER_GETHASH,0);
        hasher << pindexPrev->GetBlockHash() << nStakeModifierNew;
        nStakeModifier = hasher.GetHash().GetLow64();
    }
    else
    {
        nStakeModifier = nStakeModifierNew;
    }

    fGeneratedStakeModifier = true;
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

void SetStakeModifiersForNewBlockIndex(const BlockMap& blockIndicesByHash, CBlockIndex* pindexNew)
{
    uint64_t nStakeModifier = 0;
    bool fGeneratedStakeModifier = false;
    if (!ComputeNextStakeModifier(blockIndicesByHash, pindexNew->pprev, nStakeModifier, fGeneratedStakeModifier))
        LogPrintf("%s : ComputeNextStakeModifier() failed \n",__func__);
    pindexNew->SetStakeModifier(nStakeModifier, fGeneratedStakeModifier);
    pindexNew->nStakeModifierChecksum = GetStakeModifierChecksum(pindexNew);
    if (!CheckStakeModifierCheckpoints(pindexNew->nHeight, pindexNew->nStakeModifierChecksum))
        LogPrintf("%s : Rejected by stake modifier checkpoint height=%d, modifier=%s \n", __func__, pindexNew->nHeight, boost::lexical_cast<std::string>(nStakeModifier));
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
    if (!VerifyScript(txin.scriptSig, txPrev.vout[txin.prevout.n].scriptPubKey, POS_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&tx, 0)))
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
    if (!posGenerator.ComputeAndVerifyProofOfStake(stakingData, block.nTime, hashProofOfStake))
        return error("CheckProofOfStake() : INFO: check kernel failed on coinstake %s, hashProof=%s \n",
            block.vtx[1].ToStringShort(), hashProofOfStake); // may occur during initial download or if behind on block chain sync

    return true;
}

bool CheckCoinstakeForVaults(const CTransaction& tx, const CBlockRewards& expectedRewards,
                             const CCoinsViewCache& view)
{
    if (!tx.IsCoinStake())
        return true;

    CAmount nValueIn = 0;
    bool foundVault = false;
    CScript vaultScript;
    for (const auto& in : tx.vin) {
        const auto& prevOut = view.GetOutputFor(in);
        nValueIn += prevOut.nValue;
        if (!IsStakingVaultScript(prevOut.scriptPubKey))
            continue;

        if (foundVault) {
            /* CheckProofOfStake already verifies that all inputs used are
               from a single script.  */
            if(vaultScript != prevOut.scriptPubKey) return false;
        } else {
            foundVault = true;
            vaultScript = prevOut.scriptPubKey;
        }
    }

    if (!foundVault)
        return true;

    assert(tx.vout.size() >= 2);
    const auto& rewardOut = tx.vout[1];
    if (rewardOut.scriptPubKey != vaultScript)
        return error("%s: output is not sent back to the vault input script", __func__);
    CAmount actualOutput = rewardOut.nValue;

    /* We optionally allow splitting of the output into two (but not more),
       provided that both have a value >= 10k DIVI.  */
    constexpr CAmount MIN_FOR_SPLITTING = 10000 * COIN;
    if (tx.vout.size() >= 3) {
        const auto& out2 = tx.vout[2];
        if (actualOutput >= MIN_FOR_SPLITTING && out2.nValue >= MIN_FOR_SPLITTING
              && out2.scriptPubKey == vaultScript)
            actualOutput += out2.nValue;
    }

    const CAmount expectedOutput = nValueIn + expectedRewards.nStakeReward;
    if (actualOutput < expectedOutput)
        return error("%s: expected output to be at least %s, got only %s",
                     __func__, FormatMoney(expectedOutput), FormatMoney(actualOutput));

    return true;
}
