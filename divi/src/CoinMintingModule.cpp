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
#include <ProofOfStakeModule.h>
#include <MasternodeModule.h>

I_BlockFactory* BlockFactorySelector(
    const I_BlockSubsidyProvider& blockSubsidies,
    I_BlockTransactionCollector& blockTransactionCollector,
    I_PoSTransactionCreator& coinstakeCreator,
    const Settings& settings,
    const CChain& activeChain,
    const CChainParams& chainParameters)
{
    if(chainParameters.NetworkID()==CBaseChainParams::Network::REGTEST)
    {
        return new ExtendedBlockFactory(
            blockSubsidies,
            blockTransactionCollector,
            coinstakeCreator,
            settings,
            activeChain,
            chainParameters);
    }
    else
    {
        return new BlockFactory(
            blockSubsidies,
            blockTransactionCollector,
            coinstakeCreator,
            settings,
            activeChain,
            chainParameters);
    }
    assert(false);
}

CoinMintingModule::CoinMintingModule(
    const Settings& settings,
    CCriticalSection& mainCS,
    const CChainParams& chainParameters,
    const CChain& activeChain,
    const BlockMap& blockIndexByHash,
    const MasternodeModule& masternodeModule,
    const CFeeRate& relayTxFeeCalculator,
    CCoinsViewCache* baseCoinsViewCache,
    CTxMemPool& mempool,
    const I_PeerBlockNotifyService& peerNotifier,
    I_StakingWallet& wallet,
    BlockTimestampsByHeight& hashedBlockTimestampsByHeight,
    const CSporkManager& sporkManager
    ): posModule_(new ProofOfStakeModule(chainParameters,activeChain,blockIndexByHash))
    , blockSubsidyContainer_(new SuperblockSubsidyContainer(chainParameters))
    , blockIncentivesPopulator_(new BlockIncentivesPopulator(
        chainParameters,
        masternodeModule,
        blockSubsidyContainer_->superblockHeightValidator(),
        blockSubsidyContainer_->blockSubsidiesProvider()))
    , blockTransactionCollector_( new BlockMemoryPoolTransactionCollector(
        settings,
        baseCoinsViewCache,
        activeChain,
        blockIndexByHash,
        mempool,
        mainCS,
        relayTxFeeCalculator))
    , coinstakeTransactionCreator_( new PoSTransactionCreator(
        settings,
        chainParameters,
        activeChain,
        blockIndexByHash,
        blockSubsidyContainer_->blockSubsidiesProvider(),
        *blockIncentivesPopulator_,
        posModule_->proofOfStakeGenerator(),
        wallet,
        hashedBlockTimestampsByHeight))
    , blockFactory_(
        BlockFactorySelector(
            blockSubsidyContainer_->blockSubsidiesProvider(),
            *blockTransactionCollector_,
            *coinstakeTransactionCreator_,
            settings,
            activeChain,
            chainParameters))
    , coinMinter_( new CoinMinter(
        activeChain,
        chainParameters,
        peerNotifier,
        masternodeModule.getMasternodeSynchronization(),
        *blockFactory_,
        wallet,
        hashedBlockTimestampsByHeight))
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
    posModule_.reset();
}

I_BlockFactory& CoinMintingModule::blockFactory() const
{
    return *blockFactory_;
}

I_CoinMinter& CoinMintingModule::coinMinter() const
{
    return *coinMinter_;
}
