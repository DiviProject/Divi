#ifndef BLOCK_PROOF_PROVER_H
#define BLOCK_PROOF_PROVER_H
#include <I_BlockProofProver.h>
class I_PoSTransactionCreator;
class CChainParams;
class CChain;
class BlockProofProver final: public I_BlockProofProver
{
private:
    const CChainParams& chainParameters_;
    const I_PoSTransactionCreator& posTransactionCreator_;
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
        const I_PoSTransactionCreator& posTransactionCreator,
        const CChain& chain);
    bool attachBlockProof(
        const CBlockIndex* chainTip,
        const bool proofOfStake,
        CBlock& block) const override;
};
#endif// BLOCK_PROOF_PROVER_H