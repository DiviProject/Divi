// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCK_PROOF_VERIFIER_H
#define BLOCK_PROOF_VERIFIER_H
#include <I_BlockProofVerifier.h>
class CChainParams;
class I_DifficultyAdjuster;
class I_ProofOfStakeGenerator;
class BlockMap;
class Settings;
class CBlock;
class CBlockIndex;

class BlockProofVerifier final: public I_BlockProofVerifier
{
private:
    const CChainParams& chainParameters_;
    const I_DifficultyAdjuster& difficultyAdjuster_;
    const I_ProofOfStakeGenerator& posGenerator_;
    const BlockMap& blockIndicesByHash_;
    const Settings& settings_;
public:
    BlockProofVerifier(
        const CChainParams& chainParameters,
        const I_DifficultyAdjuster& difficultyAdjuster,
        const I_ProofOfStakeGenerator& posGenerator,
        const BlockMap& blockIndicesByHash,
        const Settings& settings);
    bool verifyBlockProof(
        const CBlockIndex* chainTip,
        const CBlock& block) const override;
};
#endif // BLOCK_PROOF_VERIFIER_H
