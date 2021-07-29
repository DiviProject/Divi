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
    const CBlockIndex* pindexRet;
    return GetNumberOfBlockConfirmations(pindexRet);
}
bool CMerkleTx::IsInMainChain() const
{
    const CBlockIndex* pindexRet;
    return GetNumberOfBlockConfirmationsINTERNAL(pindexRet) > 0;
}

int CMerkleTx::SetMerkleBranch(const CBlock& block)
{
    // Update the tx's hashBlock
    hashBlock = block.GetHash();

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

    return activeChain_.Height() - pindex->nHeight + 1;
}

std::pair<const CBlockIndex*,int> CMerkleTx::FindConfirmedBlockIndexAndDepth(bool checkMempool) const
{
    if (hashBlock == 0 || nIndex == -1)
        return std::make_pair(nullptr,(checkMempool && !mempool.exists(GetHash()))?-1:0);

    // Find the block it claims to be in
    int depth;
    CBlockIndex* pindex;
    {
        LOCK(cs_main);
        BlockMap::const_iterator mi = blockIndices_.find(hashBlock);
        if (mi == blockIndices_.end())
        {
            return std::make_pair(nullptr,(checkMempool && !mempool.exists(GetHash()))?-1:0);
        }
        pindex = (*mi).second;
        if (!pindex || !activeChain_.Contains(pindex))
        {
            return std::make_pair(nullptr,(checkMempool && !mempool.exists(GetHash()))?-1:0);
        }
        depth = activeChain_.Height() - pindex->nHeight + 1;
    }
    return std::make_pair(pindex,depth);
}

int CMerkleTx::GetNumberOfBlockConfirmationsINTERNAL(const CBlockIndex*& pindexRet) const
{
    if (hashBlock == 0 || nIndex == -1)
        return 0;

    // Find the block it claims to be in
    unsigned bestHeight;
    CBlockIndex* pindex;
    {
        LOCK(cs_main);
        BlockMap::const_iterator mi = blockIndices_.find(hashBlock);
        if (mi == blockIndices_.end())
            return 0;
        pindex = (*mi).second;
        if (!pindex || !activeChain_.Contains(pindex))
            return 0;
        bestHeight = activeChain_.Height();
    }

    // Make sure the merkle branch connects to this block
    if (!fMerkleVerified) {
        if (CBlock::CheckMerkleBranch(GetHash(), vMerkleBranch, nIndex) != pindex->hashMerkleRoot)
            return 0;
        fMerkleVerified = true;
    }

    pindexRet = pindex;
    return bestHeight - pindex->nHeight + 1;
}

int CMerkleTx::GetNumberOfBlockConfirmations(const CBlockIndex*& pindexRet) const
{
    int nResult = GetNumberOfBlockConfirmationsINTERNAL(pindexRet);
    if (nResult == 0 && !mempool.exists(GetHash()))
        return -1; // Not in chain, not in mempool

    return nResult;
}

int CMerkleTx::GetBlocksToMaturity() const
{
    if (!(IsCoinBase() || IsCoinStake()))
        return 0;
    return std::max(0, (Params().COINBASE_MATURITY() + 1) - GetNumberOfBlockConfirmations());
}