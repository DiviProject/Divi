#ifndef I_BLOCK_PROOF_VERIFIER_H
#define I_BLOCK_PROOF_VERIFIER_H
class CBlockIndex;
class CBlock;

class I_BlockProofVerifier
{
public:
    virtual ~I_BlockProofVerifier(){}
    virtual bool verifyBlockProof(
        const CBlockIndex* chainTip,
        const CBlock& block) const = 0;
};
#endif// I_BLOCK_PROOF_VERIFIER_H