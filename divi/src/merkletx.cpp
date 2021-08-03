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

CMerkleTx::CMerkleTx(
    const CTransaction& txIn,
    const I_MerkleTxConfirmationNumberCalculator& confirmationCalculator
    ): CTransaction(txIn)
    , confirmationCalculator_(confirmationCalculator)
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
int CMerkleTx::GetNumberOfBlockConfirmations() const
{
    return confirmationCalculator_.GetNumberOfBlockConfirmations(*this);
}
int CMerkleTx::GetBlocksToMaturity() const
{
    if (!(IsCoinBase() || IsCoinStake()))
        return 0;
    return std::max(0, (Params().COINBASE_MATURITY() + 1) - confirmationCalculator_.GetNumberOfBlockConfirmations(*this));
}