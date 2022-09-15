#include <BlockProofProver.h>

#include <chain.h>
#include <chainparams.h>
#include <Logging.h>
#include <primitives/block.h>
#include <timedata.h>
#include <utiltime.h>
#include <I_PoSTransactionCreator.h>

#include <boost/thread.hpp>

BlockProofProver::BlockProofProver(
    const CChainParams& chainParameters,
    const I_PoSTransactionCreator& posTransactionCreator,
    const CChain& chain
    ): chainParameters_(chainParameters)
    , posTransactionCreator_(posTransactionCreator)
    , chain_(chain)
{
}


bool BlockProofProver::attachProofOfWorkToBlock(
    const CBlockIndex* const previousBlockIndex,
    CBlock& block) const
{
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
    return posTransactionCreator_.CreateProofOfStake(previousBlockIndex, block);
}

bool BlockProofProver::attachBlockProof(
    const CBlockIndex* chainTip,
    const bool proofOfStake,
    CBlock& block) const
{
    boost::this_thread::interruption_point();
    if(!proofOfStake) return attachProofOfWorkToBlock(chainTip,block);
    else return attachProofOfStakeToBlock(chainTip,block);
}