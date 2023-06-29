#include <ChainExtensionModule.h>

#include <ChainExtensionService.h>
#include <AcceptBlockValidator.h>
#include <BlockSubmitter.h>
#include <ProofOfStakeModule.h>
#include <SuperblockSubsidyContainer.h>
#include <BlockIncentivesPopulator.h>
#include <DifficultyAdjuster.h>
#include <BlockProofVerifier.h>
#include <PoSTransactionCreator.h>
#include <BlockProofProver.h>
#include <sync.h>

ChainExtensionModule::ChainExtensionModule(
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
    BlockIndexCandidates& blockIndexCandidates
    ): chainstateManager_(chainstateManager)
    , peerIdByBlockHash_()
    , mainCriticalSection_(mainCriticalSection)
    , difficultyAdjuster_(new DifficultyAdjuster(chainParameters))
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
    , proofOfStakeModule_(
        new ProofOfStakeModule(
            chainParameters,
            chainstateManager.ActiveChain(),
            chainstateManager.GetBlockMap()))
    , posBlockProofProver_(
        new PoSTransactionCreator(
            settings,
            chainParameters,
            chainstateManager.ActiveChain(),
            chainstateManager.GetBlockMap(),
            blockSubsidies_->blockSubsidiesProvider(),
            *incentives_,
            proofOfStakeModule_->proofOfStakeGenerator(),
            mapHashedBlocks))
    , blockProofProver_(
        new BlockProofProver(
            chainParameters,
            blockSubsidies_->blockSubsidiesProvider(),
            *posBlockProofProver_,
            chainstateManager.ActiveChain()))
    , blockProofVerifier_(
        new BlockProofVerifier(
            chainParameters,
            *difficultyAdjuster_,
            proofOfStakeModule_->proofOfStakeGenerator(),
            chainstateManager.GetBlockMap(),
            settings))
    , chainExtensionService_(
        new ChainExtensionService(
            chainParameters,
            settings,
            sporkManager,
            *blockSubsidies_,
            *incentives_,
            *blockProofVerifier_,
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
    blockProofVerifier_.reset();
    blockProofProver_.reset();
    posBlockProofProver_.reset();
    proofOfStakeModule_.reset();
    incentives_.reset();
    blockSubsidies_.reset();
    difficultyAdjuster_.reset();
}

const I_DifficultyAdjuster& ChainExtensionModule::getDifficultyAdjuster() const
{
    return *difficultyAdjuster_;
}

const I_SuperblockSubsidyContainer& ChainExtensionModule::getBlockSubsidies() const
{
    return *blockSubsidies_;
}

const I_BlockIncentivesPopulator& ChainExtensionModule::getBlockIncentivesPopulator() const
{
    return *incentives_;
}

const I_BlockProofProver& ChainExtensionModule::getBlockProofProver(I_StakingWallet& stakingWallet) const
{
    LOCK(mainCriticalSection_);
    posBlockProofProver_->setWallet(stakingWallet);
    return *blockProofProver_;
}

const I_ChainExtensionService& ChainExtensionModule::getChainExtensionService() const
{
    return *chainExtensionService_;
}

const I_BlockSubmitter& ChainExtensionModule::getBlockSubmitter() const
{
    return *blockSubmitter_;
}