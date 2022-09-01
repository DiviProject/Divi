#include <MostWorkChainTransitionMediator.h>

#include <chain.h>
#include <ValidationState.h>
#include <I_ChainTipManager.h>
#include <ChainstateManager.h>
#include <sync.h>
#include <Settings.h>
#include <ForkWarningHelpers.h>
#include <BlockInvalidationHelpers.h>
#include <ChainSyncHelpers.h>


void MostWorkChainTransitionMediator::computeNextBlockIndicesToConnect(
    CBlockIndex* pindexMostWork,
    const int startingHeight,
    const int maxHeightTarget,
    std::vector<CBlockIndex*>& blockIndicesToConnect) const
{
    blockIndicesToConnect.clear();
    CBlockIndex* pindexIter = pindexMostWork->GetAncestor(maxHeightTarget);
    while (pindexIter && pindexIter->nHeight != startingHeight) {
        blockIndicesToConnect.push_back(pindexIter);
        pindexIter = pindexIter->pprev;
    }
}

bool MostWorkChainTransitionMediator::rollBackChainTipToConnectToMostWorkChain(
    const CChain& chain,
    const CBlockIndex* mostWorkBlockIndex) const
{
    const CBlockIndex* pindexFork = chain.FindFork(mostWorkBlockIndex);
    // Disconnect active blocks which are no longer in the best chain.
    while (chain.Tip() && chain.Tip() != pindexFork) {
        if (!chainTipManager_.disconnectTip(false))
            return false;
    }
    return true;
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either NULL or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool MostWorkChainTransitionMediator::checkBlockConnectionState(
    CBlockIndex* lastBlockIndex) const
{
    if (state_.IsInvalid())
    {
        // The block violates a consensus rule.
        if (!state_.CorruptionPossible())
            InvalidChainFound(IsInitialBlockDownload(mainCriticalSection_,settings_),settings_,mainCriticalSection_,lastBlockIndex);
        state_ = CValidationState();
        return false;
    }
    else
    {
        return true;
    }
}


MostWorkChainTransitionMediator::BlockConnectionResult MostWorkChainTransitionMediator::tryToConnectNextBlock(
    const CChain& chain,
    const CBlock* blockToConnect,
    const CBlockIndex* previousChainTip,
    CBlockIndex* proposedNewChainTip,
    CBlockIndex* pindexConnect) const
{
    const bool blockSuccessfullyConnected = chainTipManager_.connectTip(blockToConnect,pindexConnect);
    if (!blockSuccessfullyConnected)
    {
        if(!checkBlockConnectionState(proposedNewChainTip))
        {
            return BlockConnectionResult::INVALID_BLOCK;
        }
        else
        {
            // A system error occurred (disk space, database error, ...)
            return BlockConnectionResult::UNKNOWN_SYSTEM_ERROR;
        }
    } else {
        PruneBlockIndexCandidates(chain);
        if (!previousChainTip || chain.Tip()->nChainWork > previousChainTip->nChainWork) {
            // We're in a better position than we were. Return temporarily to release the lock.
            return BlockConnectionResult::CHAINWORK_IMPROVED;
        }
        return BlockConnectionResult::TRY_NEXT_BLOCK;
    }
}


MostWorkChainTransitionMediator::MostWorkChainTransitionMediator(
    const Settings& settings,
    CCriticalSection& mainCriticalSection,
    ChainstateManager& chainstate,
    BlockIndexSuccessorsByPreviousBlockIndex& unlinkedBlocks,
    BlockIndexCandidates& blockIndexCandidates,
    CValidationState& state,
    const I_ChainTipManager& chainTipManager
    ): settings_(settings)
    , mainCriticalSection_(mainCriticalSection)
    , chainstate_(chainstate)
    , unlinkedBlocks_(unlinkedBlocks)
    , blockIndexCandidates_(blockIndexCandidates)
    , state_(state)
    , chainTipManager_(chainTipManager)
{
}

bool MostWorkChainTransitionMediator::transitionActiveChainToMostWorkChain(
    CBlockIndex* pindexMostWork,
    const CBlock* pblock) const
{
    AssertLockHeld(mainCriticalSection_);

    const auto& chain = chainstate_.ActiveChain();
    const CBlockIndex* previousChainTip = chain.Tip();

    // Disconnect active blocks which are no longer in the best chain.
    if(!rollBackChainTipToConnectToMostWorkChain(chain, pindexMostWork)) return false;
    const CBlockIndex* rolledBackChainTip = chain.Tip();
    int nHeight = rolledBackChainTip? rolledBackChainTip->nHeight : -1;

    // Build list of new blocks to connect.
    std::vector<CBlockIndex*> blockIndicesToConnect;
    blockIndicesToConnect.reserve(32);
    BlockConnectionResult result = BlockConnectionResult::TRY_NEXT_BLOCK;
    while (nHeight != pindexMostWork->nHeight) {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        int nTargetHeight = std::min(nHeight + 32, pindexMostWork->nHeight);
        computeNextBlockIndicesToConnect(pindexMostWork,nHeight,nTargetHeight,blockIndicesToConnect);
        nHeight = nTargetHeight;

        // Connect new blocks.
        for(std::vector<CBlockIndex*>::reverse_iterator it = blockIndicesToConnect.rbegin();
            it != blockIndicesToConnect.rend() && result == BlockConnectionResult::TRY_NEXT_BLOCK;
            ++it)
        {
            CBlockIndex* pindexConnect = *it;
            const CBlock* blockToConnect = pindexConnect == pindexMostWork ? pblock : nullptr;
            result = tryToConnectNextBlock(
                chain, blockToConnect, previousChainTip,blockIndicesToConnect.back(),pindexConnect);
        }
        if(result != BlockConnectionResult::TRY_NEXT_BLOCK) break;
    }
    if(result == BlockConnectionResult::UNKNOWN_SYSTEM_ERROR) return false;

    // Callbacks/notifications for a new best chain.
    if (result == BlockConnectionResult::INVALID_BLOCK)
        CheckForkWarningConditionsOnNewFork(settings_, mainCriticalSection_,blockIndicesToConnect.back(), IsInitialBlockDownload(mainCriticalSection_,settings_));
    else
        CheckForkWarningConditions(settings_, mainCriticalSection_,IsInitialBlockDownload(mainCriticalSection_,settings_));

    return true;
}
CBlockIndex* MostWorkChainTransitionMediator::findMostWorkChain() const
{
    return FindMostWorkChain(chainstate_, unlinkedBlocks_, blockIndexCandidates_);
}