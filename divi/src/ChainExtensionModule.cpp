#include <ChainExtensionModule.h>

#include <ChainExtensionService.h>
#include <AcceptBlockValidator.h>

ChainExtensionModule::ChainExtensionModule(
    ChainstateManager& chainstateManager,
    CTxMemPool& mempool,
    const MasternodeModule& masternodeModule,
    MainNotificationSignals& mainNotificationSignals,
    CCriticalSection& mainCriticalSection,
    const Settings& settings,
    const CChainParams& chainParameters,
    const CSporkManager& sporkManager,
    BlockIndexSuccessorsByPreviousBlockIndex& blockIndexSuccessors,
    BlockIndexCandidates& blockIndexCandidates
    ): chainstateManager_(chainstateManager)
    , chainExtensionService_(
        new ChainExtensionService(
            chainstateManager_,
            mempool,
            masternodeModule,
            mainNotificationSignals,
            mainCriticalSection,
            settings,
            chainParameters,
            sporkManager,
            blockIndexSuccessors,
            blockIndexCandidates))
    , blockValidator_(
        new AcceptBlockValidator(
            *chainExtensionService_,
            mainCriticalSection,
            chainParameters,
            chainstateManager_))
{
}

ChainExtensionModule::~ChainExtensionModule()
{
    blockValidator_.reset();
    chainExtensionService_.reset();
}

const I_ChainExtensionService& ChainExtensionModule::getChainExtensionService() const
{
    return *chainExtensionService_;
}

const I_BlockValidator& ChainExtensionModule::getBlockValidator() const
{
    return *blockValidator_;
}