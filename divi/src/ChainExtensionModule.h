#ifndef CHAIN_EXTENSION_MODULE_H
#define CHAIN_EXTENSION_MODULE_H
#include <memory>
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

class ChainExtensionModule
{
private:
    ChainstateManager& chainstateManager_;
    std::unique_ptr<I_ChainExtensionService> chainExtensionService_;
    std::unique_ptr<I_BlockValidator> blockValidator_;
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
};
#endif// CHAIN_EXTENSION_MODULE_H