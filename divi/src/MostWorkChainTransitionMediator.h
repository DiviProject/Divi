#ifndef MOST_WORK_CHAIN_TRANSITION_MEDIATOR_H
#define MOST_WORK_CHAIN_TRANSITION_MEDIATOR_H
#include <BlockCheckingHelpers.h>
#include <I_MostWorkChainTransitionMediator.h>

class CValidationState;
class CBlock;
class CBlockIndex;
class ChainstateManager;
class I_ChainTipManager;
class CCriticalSections;
class Settings;

class MostWorkChainTransitionMediator final: public I_MostWorkChainTransitionMediator
{
private:
    const Settings& settings_;
    CCriticalSection& mainCriticalSection_;
    ChainstateManager& chainstate_;
    BlockIndexSuccessorsByPreviousBlockIndex& unlinkedBlocks_;
    BlockIndexCandidates& blockIndexCandidates_;
    const I_ChainTipManager& chainTipManager_;

    void computeNextBlockIndicesToConnect(
        CBlockIndex* pindexMostWork,
        const int startingHeight,
        const int maxHeightTarget,
        std::vector<CBlockIndex*>& blockIndicesToConnect) const;

    bool rollBackChainTipToConnectToMostWorkChain(
        CValidationState& state,
        const CChain& chain,
        const CBlockIndex* mostWorkBlockIndex) const;
    bool checkBlockConnectionState(
        CValidationState& state,
        CBlockIndex* lastBlockIndex) const;

    enum class BlockConnectionResult
    {
        TRY_NEXT_BLOCK,
        UNKNOWN_SYSTEM_ERROR,
        INVALID_BLOCK,
        CHAINWORK_IMPROVED,
    };

    BlockConnectionResult tryToConnectNextBlock(
        CValidationState& state,
        const CChain& chain,
        const CBlock* blockToConnect,
        const CBlockIndex* previousChainTip,
        CBlockIndex* proposedNewChainTip,
        CBlockIndex* pindexConnect) const;


public:
    MostWorkChainTransitionMediator(
        const Settings& settings,
        CCriticalSection& mainCriticalSection,
        ChainstateManager& chainstate,
        BlockIndexSuccessorsByPreviousBlockIndex& unlinkedBlocks,
        BlockIndexCandidates& blockIndexCandidates,
        const I_ChainTipManager& chainTipManager);

    bool transitionActiveChainToMostWorkChain(
        CValidationState& state,
        CBlockIndex* pindexMostWork,
        const CBlock* pblock) const override;
    CBlockIndex* findMostWorkChain() const override;
};
#endif// MOST_WORK_CHAIN_TRANSITION_MEDIATOR_H