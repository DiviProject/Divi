// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "checkpoints.h"

#include "blockmap.h"
#include "chainparams.h"
#include "uint256.h"

#include <stdint.h>

#include <boost/foreach.hpp>


/**
     * How many times we expect transactions after the last checkpoint to
     * be slower. This number is a compromise, as it can't be accurate for
     * every system. When reindexing from a fast disk with a slow CPU, it
     * can be up to 20, while when downloading from a slow network with a
     * fast multicore CPU, it won't be much higher than 1.
     */
bool CCheckpointServices::fEnabled = true;

CCheckpointServices::CCheckpointServices(
    CheckpointDataProvider checkpointDataProvider
    ): checkpointDataProvider_(checkpointDataProvider)
{
}

CCheckpointServices::CCheckpointServices(
    const CCheckpointData& staticCheckpointData
    ): checkpointDataProvider_(
        InternalCheckpointDataProvider([&staticCheckpointData]()-> const CCheckpointData&{return staticCheckpointData;} ))
{
}

bool CCheckpointServices::CheckBlock(int nHeight, const uint256& hash, bool fMatchesCheckpoint) const
{
    if (!CCheckpointServices::fEnabled)
        return true;

    const MapCheckpoints& checkpoints = *checkpointDataProvider_().mapCheckpoints;

    MapCheckpoints::const_iterator i = checkpoints.find(nHeight);
    // If looking for an exact match, then return false
    if (i == checkpoints.end()) return !fMatchesCheckpoint;
    return hash == i->second;
}

//! Guess how far we are in the verification process at the given block index
double CCheckpointServices::GuessVerificationProgress(CBlockIndex* pindex, bool useConservativeEstimate) const
{
    static const double CONSERVATIVE_VERIFICATION_FACTOR = 5.0;
    if (pindex == NULL)
        return 0.0;

    int64_t nNow = time(NULL);

    double conservativeCheckVerificationFactor = useConservativeEstimate ? CONSERVATIVE_VERIFICATION_FACTOR : 1.0;
    double fWorkBefore = 0.0; // Amount of work done before pindex
    double fWorkAfter = 0.0;  // Amount of work left after pindex (estimated)
    // Work is defined as: 1.0 per transaction before the last checkpoint, and
    // conservativeCheckVerificationFactor per transaction after.

    const CCheckpointData& data = checkpointDataProvider_();
    bool lessTxsThanLastCheckpoint = pindex->nChainTx <= data.nTransactionsLastCheckpoint;
    if (lessTxsThanLastCheckpoint) {
        double transactionCountAtBlock = pindex->nChainTx;
        double additionalTransactionsToReachCheckpoint = data.nTransactionsLastCheckpoint - pindex->nChainTx;
        double estimatedAdditionalTransactionsMissing = (nNow - data.nTimeLastCheckpoint) / 86400.0 * data.fTransactionsPerDay;
        fWorkBefore = transactionCountAtBlock;
        fWorkAfter = additionalTransactionsToReachCheckpoint + estimatedAdditionalTransactionsMissing * conservativeCheckVerificationFactor;
    } else {
        double transactionCountAtLastCheckpoint = data.nTransactionsLastCheckpoint;
        double additionalTransactionsToReachBlock = pindex->nChainTx - data.nTransactionsLastCheckpoint;
        double estimatedAdditionalTransactionsMissing = (nNow - pindex->GetBlockTime()) / 86400.0 * data.fTransactionsPerDay;
        fWorkBefore = transactionCountAtLastCheckpoint + additionalTransactionsToReachBlock * conservativeCheckVerificationFactor;
        fWorkAfter = estimatedAdditionalTransactionsMissing * conservativeCheckVerificationFactor;
    }

    return fWorkBefore / (fWorkBefore + fWorkAfter);
}

int CCheckpointServices::GetTotalBlocksEstimate() const
{
    if (!CCheckpointServices::fEnabled)
        return 0;

    const MapCheckpoints& checkpoints = *checkpointDataProvider_().mapCheckpoints;
    if(checkpoints.empty())
    {
        return 0;
    }

    return checkpoints.rbegin()->first;
}

CBlockIndex* CCheckpointServices::GetLastCheckpoint(const BlockMap& mapBlockIndex) const
{
    if (!CCheckpointServices::fEnabled)
        return NULL;

    const MapCheckpoints& checkpoints = *checkpointDataProvider_().mapCheckpoints;

    BOOST_REVERSE_FOREACH (const MapCheckpoints::value_type& i, checkpoints) {
        const uint256& hash = i.second;
        BlockMap::const_iterator t = mapBlockIndex.find(hash);
        if (t != mapBlockIndex.end())
            return t->second;
    }
    return NULL;
}