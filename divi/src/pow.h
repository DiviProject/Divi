// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H
class CChainParams;
class I_DifficultyAdjuster;
class I_ProofOfStakeGenerator;
class BlockMap;
class Settings;
class CBlock;
class CBlockIndex;

bool CheckWork(
    const CChainParams& chainParameters,
    const I_DifficultyAdjuster& difficultyAdjuster,
    const I_ProofOfStakeGenerator& posGenerator,
    const BlockMap& blockIndicesByHash,
    const Settings& settings,
    const CBlock& block,
    const CBlockIndex* const pindexPrev);
#endif // BITCOIN_POW_H
