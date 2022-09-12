#include <ChainExtensionModule.h>

#include <ChainExtensionService.h>
#include <AcceptBlockValidator.h>
#include <BlockSubmitter.h>
#include <ProofOfStakeModule.h>
#include <SuperblockSubsidyContainer.h>
#include <BlockIncentivesPopulator.h>

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
    , blockSubsidies_(
        new SuperblockSubsidyContainer(
            chainParameters,
            sporkManager))
    , incentives_(
        new BlockIncentivesPopulator(
            chainParameters,
            masternodeModule,
            blockSubsidies_->superblockHeightValidator(),
            blockSubsidies_->blockSubsidiesProvider() ))
    , proofOfStakeModule_(new ProofOfStakeModule(chainParameters,chainstateManager.ActiveChain(),chainstateManager.GetBlockMap()))
    , chainExtensionService_(
        new ChainExtensionService(
            chainParameters,
            settings,
            masternodeModule,
            sporkManager,
            *blockSubsidies_,
            *incentives_,
            proofOfStakeModule_->proofOfStakeGenerator(),
            peerIdByBlockHash_,
            chainstateManager_,
            mempool,
            mainNotificationSignals,
            mainCriticalSection,
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
    proofOfStakeModule_.reset();
    incentives_.reset();
    blockSubsidies_.reset();
}

const SuperblockSubsidyContainer& ChainExtensionModule::getBlockSubsidies() const
{
    return *blockSubsidies_;
}

const BlockIncentivesPopulator& ChainExtensionModule::getBlockIncentivesPopulator() const
{
    return *incentives_;
}

const I_ProofOfStakeGenerator& ChainExtensionModule::getProofOfStakeGenerator() const
{
    return proofOfStakeModule_->proofOfStakeGenerator();
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