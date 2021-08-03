#ifndef MERKLE_TX_H
#define MERKLE_TX_H
/** A transaction with a merkle branch linking it to the block chain. */
#include <primitives/transaction.h>
#include <uint256.h>

class CBlock;
class BlockMap;

class CMerkleTx : public CTransaction
{
private:
    bool VerifyMerkleProof(const uint256 merkleRoot) const;
protected:
    const int requiredCoinbaseMaturity_;
public:
    uint256 hashBlock;
    std::vector<uint256> vMerkleBranch;
    int merkleBranchIndex;

    // memory only
    mutable bool fMerkleVerified;
    CMerkleTx(const CTransaction& txIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(*(CTransaction*)this);
        nVersion = this->nVersion;
        READWRITE(hashBlock);
        READWRITE(vMerkleBranch);
        READWRITE(merkleBranchIndex);
        if(ser_action.ForRead() && MerkleBranchIsSet())
        {
            fMerkleVerified = true;
        }
    }

    void SetMerkleBranch(const CBlock& block);
    bool MerkleBranchIsSet() const;
    void ClearMerkleBranch();
};
#endif// MERKLE_TX_H
