#include <ChainExtensionModule.h>

#include <ChainExtensionService.h>
#include <AcceptBlockValidator.h>
#include <BlockSubmitter.h>

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
    , peerIdByBlockHash_()
    , chainExtensionService_(
        new ChainExtensionService(
            peerIdByBlockHash_,
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
            peerIdByBlockHash_,
            *chainExtensionService_,
            mainCriticalSection,
            chainParameters,
            chainstateManager_))
    , blockSubmitter_(
        new BlockSubmitter(
            *blockValidator_,
            mainCriticalSection,
            chainstateManager_))
{
}

ChainExtensionModule::~ChainExtensionModule()
{
    blockSubmitter_.reset();
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
const I_BlockSubmitter& ChainExtensionModule::getBlockSubmitter() const
{
    return *blockSubmitter_;
}