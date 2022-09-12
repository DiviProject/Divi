#ifndef I_BLOCK_PROOF_GENERATOR_H
#define I_BLOCK_PROOF_GENERATOR_H
class CBlockIndex;
class CBlock;
class I_BlockProofGenerator
{
public:
    virtual ~I_BlockProofGenerator(){}
    virtual bool attachBlockProof(
        const CBlockIndex* chainTip,
        CBlock& block) const = 0;
    virtual bool verifyBlockProof(
        const CBlockIndex* chainTip,
        const CBlock& block) const = 0;
};
#endif// I_BLOCK_PROOF_GENERATOR_H