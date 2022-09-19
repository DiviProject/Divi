#ifndef NEXT_BLOCK_TYPE_HELPERS_H
#define NEXT_BLOCK_TYPE_HELPERS_H
class CBlockIndex;
class CChainParams;
enum NextBlockType
{
    UNDEFINED_TIP,
    PROOF_OF_STAKE,
    PROOF_OF_WORK,
};
namespace NextBlockTypeHelpers
{
    NextBlockType ComputeNextBlockType(const CBlockIndex* chainTip,const int lastPOWBlock);
    bool nextBlockIsProofOfStake(const CBlockIndex* chainTip, const CChainParams& chainParameters);
} // namespace NextBlockTypeHelpers

#endif// NEXT_BLOCK_TYPE_HELPERS_H