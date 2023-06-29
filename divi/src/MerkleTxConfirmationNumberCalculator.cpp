#include <MerkleTxConfirmationNumberCalculator.h>
#include <chain.h>
#include <blockmap.h>
#include <sync.h>
#include <txmempool.h>
#include <merkletx.h>

MerkleTxConfirmationNumberCalculator::MerkleTxConfirmationNumberCalculator(
    const CChain& activeChain,
    const BlockMap& blockIndices,
    const int coinbaseConfirmationsForMaturity,
    const CTxMemPool& mempool,
    CCriticalSection& mainCS
    ): activeChain_(activeChain)
    , blockIndices_(blockIndices)
    , coinbaseConfirmationsForMaturity_(coinbaseConfirmationsForMaturity)
    , mempool_(mempool)
    , mainCS_(mainCS)
{
}

std::pair<const CBlockIndex*,int> MerkleTxConfirmationNumberCalculator::FindConfirmedBlockIndexAndDepth(const CMerkleTx& merkleTx) const
{
    static const std::pair<const CBlockIndex*,int> defaultValue = std::make_pair(nullptr,0);
    if(!merkleTx.MerkleBranchIsSet())
        return defaultValue;

    // Find the block it claims to be in
    int depth;
    CBlockIndex* pindex;
    {
        LOCK(mainCS_);
        BlockMap::const_iterator mi = blockIndices_.find(merkleTx.hashBlock);
        if (mi == blockIndices_.end())
        {
            return defaultValue;
        }
        pindex = (*mi).second;
        if (!pindex || !activeChain_.Contains(pindex))
        {
            return defaultValue;
        }
        depth = activeChain_.Height() - pindex->nHeight + 1;
    }
    return std::make_pair(pindex,depth);
}

int MerkleTxConfirmationNumberCalculator::GetNumberOfBlockConfirmations(const CMerkleTx& merkleTx) const
{
    const int depth = FindConfirmedBlockIndexAndDepth(merkleTx).second;
    if(depth==0 && !mempool_.exists(merkleTx.GetHash())) return -1;
    return depth;
}
int MerkleTxConfirmationNumberCalculator::GetBlocksToMaturity(const CMerkleTx& merkleTx) const
{
    if (!(merkleTx.IsCoinBase() || merkleTx.IsCoinStake()))
        return 0;
    return std::max(0, (coinbaseConfirmationsForMaturity_ + 1) - GetNumberOfBlockConfirmations(merkleTx));
}