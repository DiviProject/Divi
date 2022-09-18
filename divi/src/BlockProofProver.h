#ifndef BLOCK_PROOF_PROVER_H
#define BLOCK_PROOF_PROVER_H
#include <I_BlockProofProver.h>
class CChainParams;
class CChain;
class I_BlockSubsidyProvider;
class BlockProofProver final: public I_BlockProofProver
{
private:
    const CChainParams& chainParameters_;
    const I_BlockSubsidyProvider& blockSubsidies_;
    const I_BlockProofProver& proofOfStakeProver_;
    const CChain& chain_;
    bool attachProofOfWorkToBlock(
        const CBlockIndex* const previousBlockIndex,
        CBlock& block) const;
    bool attachProofOfStakeToBlock(
        const CBlockIndex* const previousBlockIndex,
        CBlock& block) const;
public:
    BlockProofProver(
        const CChainParams& chainParameters,
        const I_BlockSubsidyProvider& blockSubsidies,
        const I_BlockProofProver& proofOfStakeProver,
        const CChain& chain);
    bool attachBlockProof(
        const CBlockIndex* chainTip,
        const bool proofOfStake,
        CBlock& block) const override;
};
#endif// BLOCK_PROOF_PROVER_H