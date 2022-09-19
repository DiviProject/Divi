#include <NextBlockTypeHelpers.h>

#include <chain.h>
#include <chainparams.h>

NextBlockType NextBlockTypeHelpers::ComputeNextBlockType(const CBlockIndex* chainTip,const int lastPOWBlock)
{
    return (!chainTip)? UNDEFINED_TIP:
            (chainTip->nHeight < lastPOWBlock)? PROOF_OF_WORK: PROOF_OF_STAKE;
}
bool NextBlockTypeHelpers::nextBlockIsProofOfStake(const CBlockIndex* chainTip, const CChainParams& chainParameters)
{
    return ComputeNextBlockType(chainTip, chainParameters.LAST_POW_BLOCK()) == PROOF_OF_STAKE;
}