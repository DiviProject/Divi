// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHECKPOINTS_H
#define BITCOIN_CHECKPOINTS_H

#include "uint256.h"
#include "checkpoint_data.h"

class CBlockIndex;


/** 
 * Block-chain checkpoints are compiled-in sanity checks.
 * They are updated every release or three.
 */
class CCheckpoints
{
public:
    //! Returns true if block passes checkpoint checks
    static bool CheckBlock(int nHeight, const uint256& hash, bool fMatchesCheckpoint = false);

    //! Return conservative estimate of total number of blocks, 0 if unknown
    static int GetTotalBlocksEstimate();

    //! Returns last CBlockIndex* in mapBlockIndex that is a checkpoint
    static CBlockIndex* GetLastCheckpoint();

    static double GuessVerificationProgress(CBlockIndex* pindex, bool fSigchecks = true);

    static bool fEnabled;
}; //class CCheckpoints

#endif // BITCOIN_CHECKPOINTS_H
