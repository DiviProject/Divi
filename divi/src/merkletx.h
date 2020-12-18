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
    int GetNumberOfBlockConfirmationsINTERNAL(const CBlockIndex*& pindexRet) const;

public:
    uint256 hashBlock;
    std::vector<uint256> vMerkleBranch;
    int nIndex;

    // memory only
    mutable bool fMerkleVerified;
    CMerkleTx(const CTransaction& txIn,const CChain& activeChain, const BlockMap& blockIndices);
    CMerkleTx();
    CMerkleTx(const CTransaction& txIn);

    void Init();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(*(CTransaction*)this);
        nVersion = this->nVersion;
        READWRITE(hashBlock);
        READWRITE(vMerkleBranch);
        READWRITE(nIndex);
    }

    int SetMerkleBranch(const CBlock& block);

    /**
     * Return depth of transaction in blockchain:
     * -1  : not in blockchain, and not in memory pool (conflicted transaction)
     *  0  : in memory pool, waiting to be included in a block
     * >=1 : this many blocks deep in the main chain
     */
    int GetNumberOfBlockConfirmations(const CBlockIndex*& pindexRet, bool enableIX = true) const;
    int GetNumberOfBlockConfirmations(bool enableIX = true) const;
    bool IsInMainChain() const;
    int GetBlocksToMaturity() const;
    bool AcceptToMemoryPool(bool fLimitFree = true, bool fRejectInsaneFee = true, bool ignoreFees = false);
    int GetTransactionLockSignatures() const;
    bool IsTransactionLockTimedOut() const;
};
#endif// MERKLE_TX_H