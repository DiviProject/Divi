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

class ChainExtensionModule
{
private:
    ChainstateManager& chainstateManager_;
    mutable std::map<uint256, NodeId> peerIdByBlockHash_;
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
        BlockIndexSuccessorsByPreviousBlockIndex& blockIndexSuccessors,
        BlockIndexCandidates& blockIndexCandidates);

    ~ChainExtensionModule();
    const I_ChainExtensionService& getChainExtensionService() const;
    const I_BlockValidator& getBlockValidator() const;
    const I_BlockSubmitter& getBlockSubmitter() const;
};
#endif// CHAIN_EXTENSION_MODULE_H