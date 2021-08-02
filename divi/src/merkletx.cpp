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
    ClearMerkleBranch();
}

void CMerkleTx::SetMerkleBranch(const CBlock& block)
{
    // Locate the transaction
    for (merkleBranchIndex = 0; merkleBranchIndex < (int)block.vtx.size(); merkleBranchIndex++)
        if (block.vtx[merkleBranchIndex] == *(CTransaction*)this)
            break;
    if (merkleBranchIndex == (int)block.vtx.size()) {
        vMerkleBranch.clear();
        merkleBranchIndex = -1;
        LogPrintf("ERROR: SetMerkleBranch() : couldn't find tx in block\n");
        return;
    }

    // Update the tx's hashBlock
    hashBlock = block.GetHash();
    // Fill in merkle branch
    vMerkleBranch = block.GetMerkleBranch(merkleBranchIndex);
    if(!VerifyMerkleProof(block.hashMerkleRoot))
    {
        ClearMerkleBranch();
    }
}
bool CMerkleTx::MerkleBranchIsSet() const
{
    return !(hashBlock == 0 || merkleBranchIndex == -1);
}
void CMerkleTx::ClearMerkleBranch()
{
    hashBlock = 0;
    merkleBranchIndex = -1;
    fMerkleVerified = false;
}
bool CMerkleTx::VerifyMerkleProof(const uint256 merkleRoot) const
{
    // Make sure the merkle branch connects to this block
    if(!fMerkleVerified)
    {
        if (CBlock::CheckMerkleBranch(GetHash(), vMerkleBranch, merkleBranchIndex) != merkleRoot)
            return false;
        fMerkleVerified = true;
        return true;
    }
    return true;
}

std::pair<const CBlockIndex*,int> CMerkleTx::FindConfirmedBlockIndexAndDepth() const
{
    static const std::pair<const CBlockIndex*,int> defaultValue = std::make_pair(nullptr,0);
    if(!MerkleBranchIsSet())
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
    return std::make_pair(pindex,depth);
}

int CMerkleTx::GetBlockHeightOfFirstConfirmation() const
{
    const CBlockIndex* firstConfirmationBlockIndex = FindConfirmedBlockIndexAndDepth().first;
    return firstConfirmationBlockIndex? firstConfirmationBlockIndex->nHeight:0;
}
int CMerkleTx::GetNumberOfBlockConfirmations() const
{
    const int depth = FindConfirmedBlockIndexAndDepth().second;
    if(depth==0 && !mempool.exists(GetHash())) return -1;
    return depth;
}
int CMerkleTx::GetBlocksToMaturity() const
{
    if (!(IsCoinBase() || IsCoinStake()))
        return 0;
    return std::max(0, (Params().COINBASE_MATURITY() + 1) - GetNumberOfBlockConfirmations());
}