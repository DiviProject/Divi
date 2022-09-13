#ifndef CHAIN_EXTENSION_SERVICE_H
#define CHAIN_EXTENSION_SERVICE_H
#include <memory>
#include <map>

#include <uint256.h>
#include <stdint.h>
#include <ChainstateManager.h>
#include <NodeId.h>
#include <BlockCheckingHelpers.h>
#include <I_ChainExtensionService.h>

class CTxMemPool;
class MasternodeModule;
class MainNotificationSignals;
class CCriticalSection;
class Settings;
class CChainParams;
class CSporkManager;
class I_ChainTipManager;
class I_MostWorkChainTransitionMediator;
class I_ProofOfStakeGenerator;
class SuperblockSubsidyContainer;
class BlockIncentivesPopulator;
class BlockIndexLotteryUpdater;
class I_BlockProofVerifier;

class ChainExtensionService final: public I_ChainExtensionService
{
private:
    const CChainParams& chainParameters_;
    const Settings& settings_;
    const CSporkManager& sporkManager_;
    const I_BlockProofVerifier& blockProofVerifier_;
    CTxMemPool& mempool_;
    MainNotificationSignals& mainNotificationSignals_;
    CCriticalSection& mainCriticalSection_;
    ChainstateManager* chainstateRef_;
    BlockIndexSuccessorsByPreviousBlockIndex& blockIndexSuccessors_;
    BlockIndexCandidates& blockIndexCandidates_;
    std::unique_ptr<BlockIndexLotteryUpdater> blockIndexLotteryUpdater_;
    std::unique_ptr<I_ChainTipManager> chainTipManager_;
    std::unique_ptr<I_MostWorkChainTransitionMediator> chainTransitionMediator_;

    bool transitionToMostWorkChainTip(
        CValidationState& state,
        const CBlock* pblock) const;

public:
    ChainExtensionService(
        const CChainParams& chainParameters,
        const Settings& settings,
        const MasternodeModule& masternodeModule,
        const CSporkManager& sporkManager,
        const SuperblockSubsidyContainer& blockSubsidies,
        const BlockIncentivesPopulator& incentives,
        const I_BlockProofVerifier& blockProofVerifier,
        std::map<uint256, NodeId>& peerIdByBlockHash,
        ChainstateManager& chainstateManager,
        CTxMemPool& mempool,
        MainNotificationSignals& mainNotificationSignals,
        CCriticalSection& mainCriticalSection,
        BlockIndexSuccessorsByPreviousBlockIndex& blockIndexSuccessors,
        BlockIndexCandidates& blockIndexCandidates);

    ~ChainExtensionService();

    std::pair<CBlockIndex*, bool> assignBlockIndex(
        CBlock& block,
        CValidationState& state,
        CDiskBlockPos* dbp) const override;
    bool updateActiveChain(
        CValidationState& state,
        const CBlock* pblock) const override;
    bool invalidateBlock(CValidationState& state, CBlockIndex* blockIndex, const bool updateCoinDatabaseOnly) const override;
    bool reconsiderBlock(CValidationState& state, CBlockIndex* pindex) const override;
    bool connectGenesisBlock() const override;
};
#endif// CHAIN_EXTENSION_SERVICE_H