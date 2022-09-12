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

namespace
{

I_BlockFactory* BlockFactorySelector(
    const I_StakingWallet& wallet,
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
            wallet,
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

} // anonymous namespace

/** This is a simple wrapper around ChainstateManager::Reference, which allows
 *  us to hold such a reference (with RAII life cycle) inside the
 *  CoinMintingModule without having to include ChainstateManager.h
 *  directly in CoinMintingModule.h.  */
class CoinMintingModule::ChainstateManagerReference : public ChainstateManager::Reference
{};

CoinMintingModule::CoinMintingModule(
    const Settings& settings,
    const CChainParams& chainParameters,
    const MasternodeModule& masternodeModule,
    const SuperblockSubsidyContainer& blockSubsidies,
    const BlockIncentivesPopulator& incentives,
    const I_ProofOfStakeGenerator& proofGenerator,
    const CFeeRate& relayTxFeeCalculator,
    const I_PeerBlockNotifyService& peerNotifier,
    const I_BlockSubmitter& blockSubmitter,
    const CSporkManager& sporkManager,
    CCriticalSection& mainCS,
    CTxMemPool& mempool,
    I_StakingWallet& wallet
    ): mapHashedBlocks_()
    , chainstate_(new ChainstateManagerReference())
    , blockSubsidyContainer_(blockSubsidies)
    , blockIncentivesPopulator_(incentives)
    , blockTransactionCollector_( new BlockMemoryPoolTransactionCollector(
        settings,
        &(*chainstate_)->CoinsTip(),
        (*chainstate_)->ActiveChain(),
        (*chainstate_)->GetBlockMap(),
        mempool,
        mainCS,
        relayTxFeeCalculator))
    , coinstakeTransactionCreator_( new PoSTransactionCreator(
        settings,
        chainParameters,
        (*chainstate_)->ActiveChain(),
        (*chainstate_)->GetBlockMap(),
        blockSubsidyContainer_.blockSubsidiesProvider(),
        blockIncentivesPopulator_,
        proofGenerator,
        wallet,
        mapHashedBlocks_))
    , blockFactory_(
        BlockFactorySelector(
            wallet,
            blockSubsidyContainer_.blockSubsidiesProvider(),
            *blockTransactionCollector_,
            *coinstakeTransactionCreator_,
            settings,
            (*chainstate_)->ActiveChain(),
            chainParameters))
    , coinMinter_( new CoinMinter(
        (*chainstate_)->ActiveChain(),
        chainParameters,
        peerNotifier,
        blockSubmitter,
        masternodeModule.getMasternodeSynchronization(),
        *blockFactory_,
        wallet,
        mapHashedBlocks_))
{
}

CoinMintingModule::~CoinMintingModule()
{
    coinMinter_.reset();
    blockFactory_.reset();
    coinstakeTransactionCreator_.reset();
    blockTransactionCollector_.reset();
}

I_BlockFactory& CoinMintingModule::blockFactory() const
{
    return *blockFactory_;
}

I_CoinMinter& CoinMintingModule::coinMinter() const
{
    return *coinMinter_;
}

const CoinMintingModule::LastExtensionTimestampByBlockHeight& CoinMintingModule::GetBlockTimestampsByHeight() const
{
    return mapHashedBlocks_;
}
