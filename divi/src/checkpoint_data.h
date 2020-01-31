// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHECKPOINT_DATA_H
#define BITCOIN_CHECKPOINT_DATA_H

#include "uint256.h"
#include <map>

typedef std::map<int, uint256> MapCheckpoints;
class CCheckpointData {
public:
    const MapCheckpoints* mapCheckpoints;
    int64_t nTimeLastCheckpoint;
    int64_t nTransactionsLastCheckpoint;
    double fTransactionsPerDay;
};


#endif // BITCOIN_CHECKPOINT_DATA_H
