#include <BlockProofProver.h>

#include <chain.h>
#include <chainparams.h>
#include <Logging.h>
#include <primitives/block.h>
#include <timedata.h>
#include <utiltime.h>
#include <I_PoSTransactionCreator.h>
#include <I_BlockSubsidyProvider.h>
#include <NextBlockTypeHelpers.h>

#include <boost/thread.hpp>

BlockProofProver::BlockProofProver(
    const CChainParams& chainParameters,
    const I_BlockSubsidyProvider& blockSubsidies,
    const I_BlockProofProver& proofOfStakeProver,
    const CChain& chain
    ): chainParameters_(chainParameters)
    , blockSubsidies_(blockSubsidies)
    , proofOfStakeProver_(proofOfStakeProver)
    , chain_(chain)
{
}


bool BlockProofProver::attachProofOfWorkToBlock(
    const CBlockIndex* const previousBlockIndex,
    CBlock& block) const
{
    CMutableTransaction modifiableCoinbaseTx(block.vtx[0]);
    modifiableCoinbaseTx.vout[0].nValue = blockSubsidies_.GetBlockSubsidity(previousBlockIndex->nHeight+1).nStakeReward;
    block.vtx[0] = modifiableCoinbaseTx;

    block.hashMerkleRoot = block.BuildMerkleTree();
    block.nTime = std::max(previousBlockIndex->GetMedianTimePast() + 1, GetAdjustedTime());
    int64_t nStart = GetTime();
    uint256 hashTarget = uint256().SetCompact(block.nBits);
    while (true)
    {
        unsigned int nHashesDone = 0;
        uint256 hash;
        while (true)
        {
            hash = block.GetHash();
            if (hash <= hashTarget)
            {
                // Found a solution
                LogPrint("minting","%s: proof-of-work found  \n  hash: %s  \ntarget: %s\n",__func__, hash, hashTarget);
                return true;
            }
            block.nNonce += 1;
            nHashesDone += 1;
            if ((block.nNonce & 0xFF) == 0)
                break;
        }

        // Check for stop or if block needs to be rebuilt
        boost::this_thread::interruption_point();

        if (block.nNonce >= 0xffff0000)
            break;
        if (GetTime() - nStart > 60)
            break;
        if (previousBlockIndex != chain_.Tip())
            break;

        // Update nTime every few seconds
        block.nTime = std::max(previousBlockIndex->GetMedianTimePast() + 1, GetAdjustedTime());
        if (chainParameters_.AllowMinDifficultyBlocks())
        {
            // Changing block->nTime can change work required on testnet:
            hashTarget.SetCompact(block.nBits);
        }
    }
    return false;
}

bool BlockProofProver::attachProofOfStakeToBlock(
    const CBlockIndex* const previousBlockIndex,
    CBlock& block) const
{
    return proofOfStakeProver_.attachBlockProof(previousBlockIndex, block);
}

bool BlockProofProver::attachBlockProof(
    const CBlockIndex* chainTip,
    CBlock& block) const
{
    boost::this_thread::interruption_point();
    NextBlockType blockType = NextBlockTypeHelpers::ComputeNextBlockType(chainTip,chainParameters_.LAST_POW_BLOCK());
    if(blockType == PROOF_OF_WORK) return attachProofOfWorkToBlock(chainTip,block);
    else if(blockType == PROOF_OF_STAKE) return attachProofOfStakeToBlock(chainTip,block);
    else return false;
}