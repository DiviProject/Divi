#include <CoinMintingModule.h>
#include <BlockFactory.h>
#include <CoinMinter.h>
#include <sync.h>
#include <ExtendedBlockFactory.h>
#include <chainparams.h>
#include <BlockMemoryPoolTransactionCollector.h>
#include <PoSTransactionCreator.h>
#include <SuperblockSubsidyContainer.h>
#include <BlockIncentivesPopulator.h>
#include <LegacyPoSStakeModifierService.h>
#include <PoSStakeModifierService.h>

I_BlockFactory* BlockFactorySelector(
    I_BlockTransactionCollector& blockTransactionCollector,
    I_PoSTransactionCreator& coinstakeCreator,
    CChain& activeChain,
    const CChainParams& chainParameters,
    AnnotatedMixin<boost::recursive_mutex>& mainCS)
{
    if(chainParameters.NetworkID()==CBaseChainParams::Network::REGTEST)
    {
        return new ExtendedBlockFactory(
            blockTransactionCollector,
            coinstakeCreator,
            activeChain,
            chainParameters,
            mainCS);
    }
    else
    {
        return new BlockFactory(
            blockTransactionCollector,
            coinstakeCreator,
            activeChain,
            chainParameters,
            mainCS);
    }
    assert(false);
}

CoinMintingModule::CoinMintingModule(
    AnnotatedMixin<boost::recursive_mutex>& mainCS,
    const CChainParams& chainParameters,
    CChain& activeChain,
    CMasternodeSync& masternodeSynchronization,
    CMasternodePayments& masternodePayments,
    CTxMemPool& mempool,
    std::vector<CNode*>& peers,
    CWallet& wallet,
    int64_t& lastCoinStakeSearchInterval,
    BlockTimestampsByHeight& hashedBlockTimestampsByHeight,
    BlockMap& blockIndexByHash
    ): legacyStakeModifierService_(new LegacyPoSStakeModifierService(blockIndexByHash,activeChain))
    , stakeModifierService_(new PoSStakeModifierService(*legacyStakeModifierService_, blockIndexByHash))
    , blockSubsidyContainer_(new SuperblockSubsidyContainer(chainParameters))
    , blockIncentivesPopulator_(new BlockIncentivesPopulator(
        chainParameters,
        activeChain,
        masternodeSynchronization,
        masternodePayments,
        blockSubsidyContainer_->superblockHeightValidator(),
        blockSubsidyContainer_->blockSubsidiesProvider()))
    , blockTransactionCollector_( new BlockMemoryPoolTransactionCollector(activeChain,mempool,mainCS))
    , coinstakeTransactionCreator_( new PoSTransactionCreator(
        chainParameters,
        activeChain,
        blockIndexByHash,
        *stakeModifierService_,
        blockSubsidyContainer_->blockSubsidiesProvider(),
        *blockIncentivesPopulator_,
        wallet,
        hashedBlockTimestampsByHeight))
    , blockFactory_(
        BlockFactorySelector(
            *blockTransactionCollector_,
            *coinstakeTransactionCreator_,
            activeChain,
            chainParameters,
            mainCS))
    , coinMinter_( new CoinMinter(
        blockSubsidyContainer_->blockSubsidiesProvider(),
        *blockFactory_,
        &wallet,
        activeChain,
        chainParameters,
        peers,
        masternodeSynchronization,
        hashedBlockTimestampsByHeight,
        mempool,
        mainCS,
        lastCoinStakeSearchInterval))
{
}

CoinMintingModule::~CoinMintingModule()
{
    coinMinter_.reset();
    blockFactory_.reset();
    coinstakeTransactionCreator_.reset();
    blockTransactionCollector_.reset();
    blockIncentivesPopulator_.reset();
    blockSubsidyContainer_.reset();
    stakeModifierService_.reset();
    legacyStakeModifierService_.reset();
}

I_BlockFactory& CoinMintingModule::blockFactory() const
{
    return *blockFactory_;
}

I_CoinMinter& CoinMintingModule::coinMinter() const
{
    return *coinMinter_;
}