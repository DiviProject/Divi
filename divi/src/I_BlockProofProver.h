#ifndef I_BLOCK_PROOF_PROVER_H
#define I_BLOCK_PROOF_PROVER_H
class CBlockIndex;
class CBlock;
class I_BlockProofProver
{
public:
    virtual ~I_BlockProofProver(){}
    virtual bool attachBlockProof(
        const CBlockIndex* chainTip,
        const bool proofOfStake,
        CBlock& block) const = 0;
};
#endif// I_BLOCK_PROOF_PROVERR_H