// Copyright (c) 2016-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
// Copyright (c) 2020 The Divi Core developers 
#ifndef VERSIONBITS_H
#define VERSIONBITS_H

#include <map>
#include <BIP9Deployment.h>

class CBlockIndex;

// A map that gives the state for blocks whose height is a multiple of Period().
// The map is indexed by the block's parent, however, so all keys in the map
// will either be nullptr or a block with (height + 1) % Period() == 0.
typedef std::map<const CBlockIndex*, ThresholdState> ThresholdConditionCache;

/** Display status of an in-progress BIP9 softfork */
struct BIP9Stats {
    /** Length of blocks of the BIP9 signalling period */
    int period;
    /** Number of blocks with the version bit set required to activate the softfork */
    int threshold;
    /** Number of blocks elapsed since the beginning of the current period */
    int elapsed;
    /** Number of blocks with the version bit set since the beginning of the current period */
    int count;
    /** False if there are not enough blocks left in this period to pass activation threshold */
    bool possible;
};

/**
 * Abstract class that implements BIP9-style threshold logic, and caches results.
 */
class AbstractThresholdConditionChecker {
protected:
    const BIP9Deployment& bip_;
    virtual bool Condition(const CBlockIndex* pindex) const =0;

public:
    explicit AbstractThresholdConditionChecker(const BIP9Deployment& bip): bip_(bip){}
    AbstractThresholdConditionChecker& operator=(AbstractThresholdConditionChecker&& other)
    {
        const_cast<BIP9Deployment&>(bip_) = other.bip_;
        return *this;
    }
    /** Returns the numerical statistics of an in-progress BIP9 softfork in the current period */
    BIP9Stats GetStateStatisticsFor(const CBlockIndex* pindex) const;
    /** Returns the state for pindex A based on parent pindexPrev B. Applies any state transition if conditions are present.
     *  Caches state from first block of period. */
    ThresholdState UpdateCacheState(const CBlockIndex* pindexPrev, ThresholdConditionCache& cache) const;
    /** Returns the height since when the ThresholdState has started for pindex A based on parent pindexPrev B, all blocks of a period share the same */
    int StartingHeightOfBlockIndexState(const CBlockIndex* pindexPrev, ThresholdConditionCache& cache) const;
};

/** BIP 9 allows multiple softforks to be deployed in parallel. We cache per-period state for every one of them
 *  keyed by the bit position used to signal support. */
struct VersionBitsCache
{
    ThresholdConditionCache caches[BIP9Deployment::MAX_VERSION_BITS_DEPLOYMENTS];
    void Clear();
};

ThresholdState VersionBitsState(const CBlockIndex* pindexPrev, const BIP9Deployment& bip9Deployment, VersionBitsCache& cache);
BIP9Stats VersionBitsStatistics(const CBlockIndex* pindexPrev, const BIP9Deployment& bip9Deployment);
int VersionBitsStateSinceHeight(const CBlockIndex* pindexPrev, const BIP9Deployment& bip9Deployment, VersionBitsCache& cache);
uint32_t VersionBitsMask(const BIP9Deployment& bip9Deployment);

#endif // VERSIONBITS_H
