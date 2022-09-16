#include <CoinMintingModule.h>

#include <BlockFactory.h>
#include <ChainstateManager.h>
#include <CoinMinter.h>
#include <ExtendedBlockFactory.h>
#include <chainparams.h>
#include <BlockMemoryPoolTransactionCollector.h>
#include <PoSTransactionCreator.h>
#include <SuperblockSubsidyContainer.h>
#include <BlockIncentivesPopulator.h>
#include <MasternodeModule.h>
#include <BlockProofProver.h>

namespace
{

I_BlockFactory* BlockFactorySelector(
    const Settings& settings,
    const CChainParams& chainParameters,
    const CChain& activeChain,
    const I_DifficultyAdjuster& difficultyAdjuster,
    const I_BlockProofProver& blockProofProver,
    const I_StakingWallet& wallet,
    I_BlockTransactionCollector& blockTransactionCollector)
{
    if(chainParameters.NetworkID()==CBaseChainParams::Network::REGTEST)
    {
        return new ExtendedBlockFactory(
            settings,
            chainParameters,
            activeChain,
            difficultyAdjuster,
            blockProofProver,
            wallet,
            blockTransactionCollector);
    }
    else
    {
        return new BlockFactory(
            settings,
            chainParameters,
            activeChain,
            difficultyAdjuster,
            blockProofProver,
            blockTransactionCollector);
    }
    assert(false);
}

} // anonymous namespace

/** This is a simple wrapper around ChainstateManager::Reference, which allows
 *  us to hold such a reference (with RAII life cycle) inside the
 *  CoinMintingModule without having to include ChainstateManager.h
 *  directly in CoinMintingModule.h.  */
class ChainstateManagerReference
{
private:
    ChainstateManager::Reference chainstateRef_;
public:
    ChainstateManagerReference(): chainstateRef_()
    {
    }
    ~ChainstateManagerReference(){}

    const ChainstateManager& getChainstateManager() const
    {
        return *chainstateRef_;
    }
};

CoinMintingModule::CoinMintingModule(
    const Settings& settings,
    const CChainParams& chainParameters,
    const CMasternodeSync& masternodeSynchronization,
    const SuperblockSubsidyContainer& blockSubsidies,
    const BlockIncentivesPopulator& incentives,
    const I_ProofOfStakeGenerator& proofGenerator,
    const CFeeRate& relayTxFeeCalculator,
    const I_PeerBlockNotifyService& peerNotifier,
    const I_BlockSubmitter& blockSubmitter,
    const CSporkManager& sporkManager,
    const I_DifficultyAdjuster& difficultyAdjuster,
    std::map<unsigned int, unsigned int>& mapHashedBlocks,
    CCriticalSection& mainCS,
    CTxMemPool& mempool,
    I_StakingWallet& wallet
    ): mapHashedBlocks_(mapHashedBlocks)
    , chainstateRef_(new ChainstateManagerReference())
    , blockSubsidyContainer_(blockSubsidies)
    , blockIncentivesPopulator_(incentives)
    , blockTransactionCollector_(
        new BlockMemoryPoolTransactionCollector(
            settings,
            &chainstateRef_->getChainstateManager().CoinsTip(),
            chainstateRef_->getChainstateManager().ActiveChain(),
            chainstateRef_->getChainstateManager().GetBlockMap(),
            mempool,
            mainCS,
            relayTxFeeCalculator))
    , coinstakeTransactionCreator_(
        new PoSTransactionCreator(
            settings,
            chainParameters,
            chainstateRef_->getChainstateManager().ActiveChain(),
            chainstateRef_->getChainstateManager().GetBlockMap(),
            blockSubsidyContainer_.blockSubsidiesProvider(),
            blockIncentivesPopulator_,
            proofGenerator,
            wallet,
            mapHashedBlocks_))
    , blockProofProver_(
        new BlockProofProver(
            chainParameters,
            blockSubsidyContainer_.blockSubsidiesProvider(),
            *coinstakeTransactionCreator_,
            chainstateRef_->getChainstateManager().ActiveChain() ))
    , blockFactory_(
        BlockFactorySelector(
            settings,
            chainParameters,
            chainstateRef_->getChainstateManager().ActiveChain(),
            difficultyAdjuster,
            *blockProofProver_,
            wallet,
            *blockTransactionCollector_))
    , coinMinter_(
        new CoinMinter(
            chainstateRef_->getChainstateManager().ActiveChain(),
            chainParameters,
            peerNotifier,
            blockSubmitter,
            masternodeSynchronization,
            *blockFactory_,
            wallet,
            mapHashedBlocks_))
{
}

CoinMintingModule::~CoinMintingModule()
{
    coinMinter_.reset();
    blockFactory_.reset();
    blockProofProver_.reset();
    coinstakeTransactionCreator_.reset();
    blockTransactionCollector_.reset();
    chainstateRef_.reset();
}

I_BlockFactory& CoinMintingModule::blockFactory() const
{
    return *blockFactory_;
}

I_CoinMinter& CoinMintingModule::coinMinter() const
{
    return *coinMinter_;
}