#ifndef MERKLE_TX_H
#define MERKLE_TX_H
/** A transaction with a merkle branch linking it to the block chain. */
#include <primitives/transaction.h>
#include <uint256.h>

class CBlockIndex;
class CBlock;
class CChain;
class BlockMap;

class CMerkleTx : public CTransaction
{
protected:
    const CChain& activeChain_;
    const BlockMap& blockIndices_;
private:
    bool VerifyMerkleBranchMatchesBlockIndex(const CBlockIndex* blockIndexOfFirstConfirmation) const;
public:
    uint256 hashBlock;
    std::vector<uint256> vMerkleBranch;
    int merkleBranchIndex;

    // memory only
    mutable bool fMerkleVerified;
    CMerkleTx(const CTransaction& txIn,const CChain& activeChain, const BlockMap& blockIndices);

    void Init();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(*(CTransaction*)this);
        nVersion = this->nVersion;
        READWRITE(hashBlock);
        READWRITE(vMerkleBranch);
        READWRITE(merkleBranchIndex);
    }

    void SetMerkleBranch(const CBlock& block);

    /**
     * Return first confirmation block index and depth of transaction in blockchain:
     * -1  : not in blockchain, and not in memory pool (conflicted transaction)
     *  0  : in memory pool, waiting to be included in a block
     * >=1 : this many blocks deep in the main chain
     */
    std::pair<const CBlockIndex*,int> FindConfirmedBlockIndexAndDepth() const;
    int GetNumberOfBlockConfirmations() const;
    int GetBlocksToMaturity() const;
};
#endif// MERKLE_TX_H
