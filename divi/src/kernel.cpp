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

// select a block from the candidate blocks in timestampSortedBlockHashes, excluding
// already selected blocks in vSelectedBlocks, and with timestamp up to
// timestampUpperBound.
static const CBlockIndex* SelectBlockIndexWithTimestampUpperBound(
    const BlockMap& blockIndicesByHash,
    const std::vector<std::pair<int64_t, uint256> >& timestampSortedBlockHashes,
    const std::set<uint256>& selectedBlockHashes,
    const int64_t timestampUpperBound,
    const uint64_t lastStakeModifier)
{
    bool fSelected = false;
    uint256 hashBest = 0;
    const CBlockIndex* pindexSelected = nullptr;
    for (const std::pair<int64_t, uint256>& item: timestampSortedBlockHashes)
    {
        if (!blockIndicesByHash.count(item.second))
        {
            error("%s: failed to find block index for candidate block %s",__func__, item.second);
            return nullptr;
        }

        const CBlockIndex* pindex = blockIndicesByHash.find(item.second)->second;
        if (fSelected && pindex->GetBlockTime() > timestampUpperBound)
            break;

        if (selectedBlockHashes.count(pindex->GetBlockHash()) > 0)
            continue;

        // compute the selection hash by hashing an input that is unique to that block
        const uint256 blockSelectionRandomnessSeed = pindex->IsProofOfStake() ? 0 : pindex->GetBlockHash();

        // the selection hash is divided by 2**32 so that proof-of-stake block
        // is always favored over proof-of-work block. this is to preserve
        // the energy efficiency property
        CDataStream ss(SER_GETHASH, 0);
        ss << blockSelectionRandomnessSeed << lastStakeModifier;
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
struct RecentBlockHashesSortedByIncreasingTimestamp
{
    std::vector<std::pair<int64_t, uint256> > timestampSortedBlockHashes;
    int64_t timestampLowerBound;

    RecentBlockHashesSortedByIncreasingTimestamp(int64_t smallestTimestamp): timestampSortedBlockHashes(),timestampLowerBound(smallestTimestamp)
    {
        timestampSortedBlockHashes.reserve(64);
    }

    void recordBlockHashesAndTimestamps(const CBlockIndex* blockIndex)
    {
        while (blockIndex && blockIndex->GetBlockTime() >= timestampLowerBound)
        {
            timestampSortedBlockHashes.push_back(std::make_pair(blockIndex->GetBlockTime(), blockIndex->GetBlockHash()));
            blockIndex = blockIndex->pprev;
        }

        std::reverse(timestampSortedBlockHashes.begin(), timestampSortedBlockHashes.end());
        std::sort(timestampSortedBlockHashes.begin(), timestampSortedBlockHashes.end());
    }
};

RecentBlockHashesSortedByIncreasingTimestamp GetRecentBlocksSortedByIncreasingTimestamp(const CBlockIndex* pindexPrev)
{
    // Sort candidate blocks by timestamp
    int64_t blockSelectionTimestampLowerBound = (pindexPrev->GetBlockTime() / MODIFIER_INTERVAL) * MODIFIER_INTERVAL - GetStakeModifierSelectionInterval();
    RecentBlockHashesSortedByIncreasingTimestamp sortedBlockHashes(blockSelectionTimestampLowerBound);
    sortedBlockHashes.recordBlockHashesAndTimestamps(pindexPrev);
    return sortedBlockHashes;
}
bool ComputeNextStakeModifier(
    const BlockMap& blockIndicesByHash,
    const CBlockIndex* pindexPrev,
    uint64_t& nextStakeModifier,
    bool& fGeneratedStakeModifier)
{
    nextStakeModifier = 0;
    fGeneratedStakeModifier = false;
    if (!pindexPrev) {
        fGeneratedStakeModifier = true;
        return true; // genesis block's modifier is 0
    }
    if (pindexPrev->nHeight == 0) {
        //Give a stake modifier to the first block
        fGeneratedStakeModifier = true;
        nextStakeModifier = 0x7374616b656d6f64;
        return true;
    }

    // First find current stake modifier and its generation block time
    // if it's not old enough, return the same stake modifier
    const CBlockIndex* indexWhereLastStakeModifierWasSet = GetLastBlockIndexWithGeneratedStakeModifier(pindexPrev);
    if (!indexWhereLastStakeModifierWasSet || !indexWhereLastStakeModifierWasSet->GeneratedStakeModifier())
        return error("ComputeNextStakeModifier: unable to get last modifier prior to blockhash %s\n",pindexPrev->GetBlockHash());

    if (indexWhereLastStakeModifierWasSet->GetBlockTime() / MODIFIER_INTERVAL >= pindexPrev->GetBlockTime() / MODIFIER_INTERVAL)
    {
        nextStakeModifier = indexWhereLastStakeModifierWasSet->nStakeModifier;
        return true;
    }

    uint64_t nStakeModifierNew = 0;
    RecentBlockHashesSortedByIncreasingTimestamp recentBlockHashesAndTimestamps = GetRecentBlocksSortedByIncreasingTimestamp(pindexPrev);

    const std::vector<std::pair<int64_t, uint256> >& timestampSortedBlockHashes = recentBlockHashesAndTimestamps.timestampSortedBlockHashes;
    int64_t timestampUpperBound = recentBlockHashesAndTimestamps.timestampLowerBound;
    std::set<uint256> selectedBlockHashes;
    for (int nRound = 0; nRound < std::min(64, (int)timestampSortedBlockHashes.size()); nRound++) {
        timestampUpperBound += GetStakeModifierSelectionIntervalSection(nRound);
        const CBlockIndex* pindex = SelectBlockIndexWithTimestampUpperBound(
            blockIndicesByHash, timestampSortedBlockHashes, selectedBlockHashes, timestampUpperBound, indexWhereLastStakeModifierWasSet->nStakeModifier);
        if (!pindex) return error("ComputeNextStakeModifier: unable to select block at round %d", nRound);

        nStakeModifierNew |= (((uint64_t)pindex->GetStakeEntropyBit()) << nRound);
        selectedBlockHashes.insert(pindex->GetBlockHash());
    }

    if(ActivationState(pindexPrev).IsActive(Fork::HardenedStakeModifier))
    {
        CHashWriter hasher(SER_GETHASH,0);
        hasher << pindexPrev->GetBlockHash() << nStakeModifierNew;
        nextStakeModifier = hasher.GetHash().GetLow64();
    }
    else
    {
        nextStakeModifier = nStakeModifierNew;
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
    uint64_t nextStakeModifier = 0;
    bool fGeneratedStakeModifier = false;
    if (!ComputeNextStakeModifier(blockIndicesByHash, pindexNew->pprev, nextStakeModifier, fGeneratedStakeModifier))
        LogPrintf("%s : ComputeNextStakeModifier() failed \n",__func__);
    pindexNew->SetStakeModifier(nextStakeModifier, fGeneratedStakeModifier);
    pindexNew->nStakeModifierChecksum = GetStakeModifierChecksum(pindexNew);
    if (!CheckStakeModifierCheckpoints(pindexNew->nHeight, pindexNew->nStakeModifierChecksum))
        LogPrintf("%s : Rejected by stake modifier checkpoint height=%d, modifier=%s \n", __func__, pindexNew->nHeight, boost::lexical_cast<std::string>(nextStakeModifier));
}