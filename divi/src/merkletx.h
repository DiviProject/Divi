#ifndef MERKLE_TX_H
#define MERKLE_TX_H
/** A transaction with a merkle branch linking it to the block chain. */
#include <primitives/transaction.h>
#include <uint256.h>
#include <boost/thread/recursive_mutex.hpp>

class CBlockIndex;
class CBlock;
class CChain;
class BlockMap;

class CTxMemPool;
class CMerkleTx;

template <typename MutexObj>
class AnnotatedMixin;

class MerkleTxConfirmationNumberCalculator
{
private:
    const CChain& activeChain_;
    const BlockMap& blockIndices_;
    CTxMemPool& mempool_;
    AnnotatedMixin<boost::recursive_mutex>& mainCS_;
public:
    MerkleTxConfirmationNumberCalculator(
        const CChain& activeChain,
        const BlockMap& blockIndices,
        CTxMemPool& mempool,
        AnnotatedMixin<boost::recursive_mutex>& mainCS);
    std::pair<const CBlockIndex*,int> FindConfirmedBlockIndexAndDepth(const CMerkleTx& merkleTx) const;
    int GetBlockHeightOfFirstConfirmation(const CMerkleTx& merkleTx) const;
    int GetNumberOfBlockConfirmations(const CMerkleTx& merkleTx) const;
};


class CMerkleTx : public CTransaction
{
protected:
    const MerkleTxConfirmationNumberCalculator& confirmationCalculator_;
private:
    bool VerifyMerkleProof(const uint256 merkleRoot) const;
public:
    uint256 hashBlock;
    std::vector<uint256> vMerkleBranch;
    int merkleBranchIndex;

    // memory only
    mutable bool fMerkleVerified;
    CMerkleTx(
        const CTransaction& txIn,
        const MerkleTxConfirmationNumberCalculator& confirmationCalculator);

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

    /**
     * Return first confirmation block index and depth of transaction in blockchain:
     * -1  : not in blockchain, and not in memory pool (conflicted transaction)
     *  0  : in memory pool, waiting to be included in a block
     * >=1 : this many blocks deep in the main chain
     */
    int GetBlockHeightOfFirstConfirmation() const;
    int GetNumberOfBlockConfirmations() const;
    int GetBlocksToMaturity() const;
};
#endif// MERKLE_TX_H
