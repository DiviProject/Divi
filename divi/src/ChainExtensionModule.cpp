#include <ChainExtensionModule.h>

#include <ChainExtensionService.h>
#include <AcceptBlockValidator.h>
#include <BlockSubmitter.h>
#include <ProofOfStakeModule.h>
#include <SuperblockSubsidyContainer.h>
#include <BlockIncentivesPopulator.h>
#include <I_DifficultyAdjuster.h>


#include <chain.h>
#include <chainparams.h>
unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CChainParams& chainParameters)
{
    /* current difficulty formula, divi - DarkGravity v3, written by Evan Duffield - evan@dashpay.io */
    const CBlockIndex* BlockLastSolved = pindexLast;
    const CBlockIndex* BlockReading = pindexLast;
    int64_t nActualTimespan = 0;
    int64_t LastBlockTime = 0;
    int64_t PastBlocksMin = 24;
    int64_t PastBlocksMax = 24;
    int64_t CountBlocks = 0;
    uint256 PastDifficultyAverage;
    uint256 PastDifficultyAveragePrev;

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || BlockLastSolved->nHeight < PastBlocksMin) {
        return chainParameters.ProofOfWorkLimit().GetCompact();
    }

    if (!chainParameters.RetargetDifficulty())
        return BlockLastSolved->nBits;

    if (pindexLast->nHeight > chainParameters.LAST_POW_BLOCK()) {
        uint256 bnTargetLimit = (~uint256(0) >> 24);
        int64_t nTargetSpacing = 60;
        int64_t nTargetTimespan = 60 * 40;

        int64_t nActualSpacing = 0;
        if (pindexLast->nHeight != 0)
            nActualSpacing = pindexLast->GetBlockTime() - pindexLast->pprev->GetBlockTime();

        if (nActualSpacing < 0)
            nActualSpacing = 1;

        // ppcoin: target change every block
        // ppcoin: retarget with exponential moving toward target spacing
        uint256 bnNew;
        bnNew.SetCompact(pindexLast->nBits);

        int64_t nInterval = nTargetTimespan / nTargetSpacing;
        bnNew *= ((nInterval - 1) * nTargetSpacing + nActualSpacing + nActualSpacing);
        bnNew /= ((nInterval + 1) * nTargetSpacing);

        if (bnNew <= 0 || bnNew > bnTargetLimit)
            bnNew = bnTargetLimit;

        return bnNew.GetCompact();
    }

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) {
            break;
        }
        CountBlocks++;

        if (CountBlocks <= PastBlocksMin) {
            if (CountBlocks == 1) {
                PastDifficultyAverage.SetCompact(BlockReading->nBits);
            } else {
                PastDifficultyAverage = ((PastDifficultyAveragePrev * CountBlocks) + (uint256().SetCompact(BlockReading->nBits))) / (CountBlocks + 1);
            }
            PastDifficultyAveragePrev = PastDifficultyAverage;
        }

        if (LastBlockTime > 0) {
            int64_t Diff = (LastBlockTime - BlockReading->GetBlockTime());
            nActualTimespan += Diff;
        }
        LastBlockTime = BlockReading->GetBlockTime();

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    uint256 bnNew(PastDifficultyAverage);

    int64_t _nTargetTimespan = CountBlocks * chainParameters.TargetSpacing();

    if (nActualTimespan < _nTargetTimespan / 3)
        nActualTimespan = _nTargetTimespan / 3;
    if (nActualTimespan > _nTargetTimespan * 3)
        nActualTimespan = _nTargetTimespan * 3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= _nTargetTimespan;

    if (bnNew > chainParameters.ProofOfWorkLimit()) {
        bnNew = chainParameters.ProofOfWorkLimit();
    }

    return bnNew.GetCompact();
}

class DifficultyAdjuster final: public I_DifficultyAdjuster
{
private:
    const CChainParams& chainParameters_;
public:
    DifficultyAdjuster(const CChainParams& chainParameters): chainParameters_(chainParameters) {}
    unsigned computeNextBlockDifficulty(const CBlockIndex* chainTip) const override
    {
        return GetNextWorkRequired(chainTip,chainParameters_);
    }
};

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
            *difficultyAdjuster_,
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
    difficultyAdjuster_.reset();
}

const I_DifficultyAdjuster& ChainExtensionModule::getDifficultyAdjuster() const
{
    return *difficultyAdjuster_;
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