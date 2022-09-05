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
class ProofOfStakeModule;

class ChainExtensionService final: public I_ChainExtensionService
{
private:
    int64_t& timeOfLastChainTipUpdate_;
    CTxMemPool& mempool_;
    const MasternodeModule& masternodeModule_;
    MainNotificationSignals& mainNotificationSignals_;
    CCriticalSection& mainCriticalSection_;
    const Settings& settings_;
    const CChainParams& chainParameters_;
    const CSporkManager& sporkManager_;
    mutable ChainstateManager::Reference chainstateRef_;
    mutable std::map<uint256, NodeId> peerIdByBlockHash_;
    BlockIndexSuccessorsByPreviousBlockIndex& blockIndexSuccessors_;
    BlockIndexCandidates& blockIndexCandidates_;
    std::unique_ptr<I_ChainTipManager> chainTipManager_;
    std::unique_ptr<I_MostWorkChainTransitionMediator> chainTransitionMediator_;
    std::unique_ptr<const ProofOfStakeModule> posModule_;

    bool transitionToMostWorkChainTip(
        CValidationState& state,
        ChainstateManager& chainstate,
        const CBlock* pblock) const;

public:
    ChainExtensionService(
        int64_t& timeOfLastChainTipUpdate,
        CTxMemPool& mempool,
        const MasternodeModule& masternodeModule,
        MainNotificationSignals& mainNotificationSignals,
        CCriticalSection& mainCriticalSection,
        const Settings& settings,
        const CChainParams& chainParameters,
        const CSporkManager& sporkManager,
        BlockIndexSuccessorsByPreviousBlockIndex& blockIndexSuccessors,
        BlockIndexCandidates& blockIndexCandidates);

    void recordBlockSource(
        const uint256& blockHash,
        NodeId nodeId) const override;
    bool assignBlockIndex(
        CBlock& block,
        CValidationState& state,
        CBlockIndex** ppindex,
        CDiskBlockPos* dbp) const override;
    bool updateActiveChain(
        CValidationState& state,
        const CBlock* pblock) const override;
    bool invalidateBlock(CValidationState& state, CBlockIndex* blockIndex, const bool updateCoinDatabaseOnly) const override;
    bool reconsiderBlock(CValidationState& state, CBlockIndex* pindex) const override;
    bool connectGenesisBlock() const override;
};
#endif// CHAIN_EXTENSION_SERVICE_H