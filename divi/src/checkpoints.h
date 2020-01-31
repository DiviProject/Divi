// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHECKPOINTS_H
#define BITCOIN_CHECKPOINTS_H

#include "uint256.h"
#include "checkpoint_data.h"
#include <functional>

class CBlockIndex;
class CChainParams;

/** 
 * Block-chain checkpoints are compiled-in sanity checks.
 * They are updated every release or three.
 */
class CCheckpoints
{
private:
    static std::function<const CChainParams&()> chainParameters_;
public:
    //! Returns true if block passes checkpoint checks
    bool CheckBlock(int nHeight, const uint256& hash, bool fMatchesCheckpoint = false) const;

    //! Return conservative estimate of total number of blocks, 0 if unknown
    int GetTotalBlocksEstimate() const;

    //! Returns last CBlockIndex* in mapBlockIndex that is a checkpoint
    CBlockIndex* GetLastCheckpoint() const;

    double GuessVerificationProgress(CBlockIndex* pindex, bool fSigchecks = true) const;

    static bool fEnabled;
}; //class CCheckpoints

#endif // BITCOIN_CHECKPOINTS_H
