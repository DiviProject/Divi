#ifndef CHAIN_EXTENSION_MODULE_H
#define CHAIN_EXTENSION_MODULE_H
#include <memory>
#include <map>

#include <NodeId.h>
#include <uint256.h>
#include <BlockIndexWork.h>
class I_ChainExtensionService;
class I_BlockValidator;
class CTxMemPool;
class MasternodeModule;
class CCriticalSection;
class MainNotificationSignals;
class Settings;
class CChainParams;
class CSporkManager;
class ChainstateManager;
class I_BlockSubmitter;
class ProofOfStakeModule;
class I_SuperblockSubsidyContainer;
class I_BlockIncentivesPopulator;
class I_DifficultyAdjuster;
class I_BlockProofVerifier;
class I_BlockProofProver;
class PoSTransactionCreator;
class I_StakingWallet;

class ChainExtensionModule
{
private:
    ChainstateManager& chainstateManager_;
    mutable std::map<uint256, NodeId> peerIdByBlockHash_;
    CCriticalSection& mainCriticalSection_;
    std::unique_ptr<I_DifficultyAdjuster> difficultyAdjuster_;
    std::unique_ptr<const I_SuperblockSubsidyContainer> blockSubsidies_;
    std::unique_ptr<const I_BlockIncentivesPopulator> incentives_;
    std::unique_ptr<const ProofOfStakeModule> proofOfStakeModule_;
    std::unique_ptr<PoSTransactionCreator> posBlockProofProver_;
    std::unique_ptr<I_BlockProofProver> blockProofProver_;
    std::unique_ptr<I_BlockProofVerifier> blockProofVerifier_;
    std::unique_ptr<I_ChainExtensionService> chainExtensionService_;
    std::unique_ptr<I_BlockValidator> blockValidator_;
    std::unique_ptr<I_BlockSubmitter> blockSubmitter_;
public:
    ChainExtensionModule(
        ChainstateManager& chainstateManager,
        CTxMemPool& mempool,
        const MasternodeModule& masternodeModule,
        MainNotificationSignals& mainNotificationSignals,
        CCriticalSection& mainCriticalSection,
        const Settings& settings,
        const CChainParams& chainParameters,
        const CSporkManager& sporkManager,
        std::map<unsigned int, unsigned int>& mapHashedBlocks,
        BlockIndexSuccessorsByPreviousBlockIndex& blockIndexSuccessors,
        BlockIndexCandidates& blockIndexCandidates);

    ~ChainExtensionModule();
    const I_DifficultyAdjuster& getDifficultyAdjuster() const;
    const I_SuperblockSubsidyContainer& getBlockSubsidies() const;
    const I_BlockIncentivesPopulator& getBlockIncentivesPopulator() const;
    const I_BlockProofProver& getBlockProofProver(I_StakingWallet& stakingWallet) const;
    const I_ChainExtensionService& getChainExtensionService() const;
    const I_BlockSubmitter& getBlockSubmitter() const;
};
#endif// CHAIN_EXTENSION_MODULE_H