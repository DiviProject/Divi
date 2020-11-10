#include <LegacyPoSStakeModifierService.h>
#include <blockmap.h>
#include <chain.h>
#include <StakeModifierIntervalHelpers.h>

LegacyPoSStakeModifierService::LegacyPoSStakeModifierService(
    const BlockMap& blockIndexByHash,
    const CChain& activeChain
    ): blockIndexByHash_(blockIndexByHash)
    , activeChain_(activeChain)
{
}

std::pair<uint64_t,bool> LegacyPoSStakeModifierService::getStakeModifier(const uint256& blockHash) const
{
    if (!blockIndexByHash_.count(blockHash))
    {
        error("GetKernelStakeModifier() : block not indexed");
        LogPrintf("CreateHashProofForProofOfStake(): failed to get kernel stake modifier \n");
        return std::make_pair(0,false);
    }
    uint64_t nStakeModifier = GetKernelStakeModifier(blockHash);
    return std::make_pair(nStakeModifier,true);
}

uint64_t LegacyPoSStakeModifierService::GetKernelStakeModifier(uint256 hashBlockFrom) const
{
    const CBlockIndex& stakeTransactionBlockIndex = *(blockIndexByHash_.find(hashBlockFrom)->second);
    int64_t timeStampOfSelectedBlock = stakeTransactionBlockIndex.GetBlockTime();
    const int64_t timeWindowForSelectingStakeModifier = GetStakeModifierSelectionInterval();
    const CBlockIndex* pindex = &stakeTransactionBlockIndex;
    CBlockIndex* pindexNext = activeChain_[stakeTransactionBlockIndex.nHeight + 1];

    // loop to find the stake modifier later by a selection interval
    while (timeStampOfSelectedBlock < stakeTransactionBlockIndex.GetBlockTime() + timeWindowForSelectingStakeModifier) {
        if (!pindexNext) {
            // Should never happen
            timeStampOfSelectedBlock = stakeTransactionBlockIndex.GetBlockTime();
            if(pindex->GeneratedStakeModifier())
            {
                return pindex->nStakeModifier;
            }
            return 0;
        }

        pindex = pindexNext;
        pindexNext = activeChain_[pindexNext->nHeight + 1];
        if (pindex->GeneratedStakeModifier()) {
            timeStampOfSelectedBlock = pindex->GetBlockTime();
        }
    }
    return pindex->nStakeModifier;
}