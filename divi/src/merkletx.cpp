#include <merkletx.h>
#include <sync.h>
#include <primitives/block.h>
#include <chainparams.h>
#include <chain.h>
#include <txmempool.h>
#include <ValidationState.h>
#include <spork.h>
#include <blockmap.h>
#include <Logging.h>
#include <utiltime.h>

extern CCriticalSection cs_main;
extern CTxMemPool mempool;


CMerkleTx::CMerkleTx(
    const CTransaction& txIn,
    const CChain& activeChain,
    const BlockMap& blockIndices
    ): CTransaction(txIn)
    , activeChain_(activeChain)
    , blockIndices_(blockIndices)
{
    Init();
}

void CMerkleTx::Init()
{
    hashBlock = 0;
    nIndex = -1;
    fMerkleVerified = false;
}

int CMerkleTx::GetNumberOfBlockConfirmations() const
{
    const int depth = FindConfirmedBlockIndexAndDepth().second;
    if(depth==0 && !mempool.exists(GetHash())) return -1;
    return depth;
}
bool CMerkleTx::IsInMainChain() const
{
    return FindConfirmedBlockIndexAndDepth().second > 0;
}

int CMerkleTx::SetMerkleBranch(const CBlock& block)
{
    // Locate the transaction
    for (nIndex = 0; nIndex < (int)block.vtx.size(); nIndex++)
        if (block.vtx[nIndex] == *(CTransaction*)this)
            break;
    if (nIndex == (int)block.vtx.size()) {
        vMerkleBranch.clear();
        nIndex = -1;
        LogPrintf("ERROR: SetMerkleBranch() : couldn't find tx in block\n");
        return 0;
    }

    // Update the tx's hashBlock
    hashBlock = block.GetHash();
    // Fill in merkle branch
    vMerkleBranch = block.GetMerkleBranch(nIndex);

    // Is the tx in a block that's in the main chain
    LOCK(cs_main);
    BlockMap::const_iterator mi = blockIndices_.find(hashBlock);
    if (mi == blockIndices_.end())
        return 0;
    const CBlockIndex* pindex = (*mi).second;
    if (!pindex || !activeChain_.Contains(pindex))
        return 0;

    VerifyMerkleBranchMatchesBlockIndex(pindex);
    return activeChain_.Height() - pindex->nHeight + 1;
}
bool CMerkleTx::VerifyMerkleBranchMatchesBlockIndex(const CBlockIndex* blockIndexOfFirstConfirmation) const
{
    // Make sure the merkle branch connects to this block
    if(!fMerkleVerified)
    {
        if (CBlock::CheckMerkleBranch(GetHash(), vMerkleBranch, nIndex) != blockIndexOfFirstConfirmation->hashMerkleRoot)
            return false;
        fMerkleVerified = true;
        return true;
    }
    return true;
}

std::pair<const CBlockIndex*,int> CMerkleTx::FindConfirmedBlockIndexAndDepth() const
{
    static const std::pair<const CBlockIndex*,int> defaultValue = std::make_pair(nullptr,0);
    if (hashBlock == 0 || nIndex == -1)
        return defaultValue;

    // Find the block it claims to be in
    int depth;
    CBlockIndex* pindex;
    {
        LOCK(cs_main);
        BlockMap::const_iterator mi = blockIndices_.find(hashBlock);
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
    if(!VerifyMerkleBranchMatchesBlockIndex(pindex))
    {
        return defaultValue;
    }
    return std::make_pair(pindex,depth);
}

int CMerkleTx::GetBlocksToMaturity() const
{
    if (!(IsCoinBase() || IsCoinStake()))
        return 0;
    return std::max(0, (Params().COINBASE_MATURITY() + 1) - GetNumberOfBlockConfirmations());
}