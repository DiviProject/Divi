#include <CoinMintingModule.h>

#include <I_BlockIncentivesPopulator.h>
#include <I_SuperblockSubsidyContainer.h>

#include <BlockFactory.h>
#include <BlockMemoryPoolTransactionCollector.h>
#include <BlockProofProver.h>
#include <chainparams.h>
#include <ChainstateManager.h>
#include <CoinMinter.h>
#include <ExtendedBlockFactory.h>
#include <PoSTransactionCreator.h>


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
    const I_BlockProofProver& blockProofProver,
    const CMasternodeSync& masternodeSynchronization,
    const CFeeRate& relayTxFeeCalculator,
    const I_PeerBlockNotifyService& peerNotifier,
    const I_BlockSubmitter& blockSubmitter,
    const I_DifficultyAdjuster& difficultyAdjuster,
    std::map<unsigned int, unsigned int>& mapHashedBlocks,
    CCriticalSection& mainCS,
    CTxMemPool& mempool,
    I_StakingWallet& wallet
    ): chainstateRef_(new ChainstateManagerReference())
    , blockTransactionCollector_(
        new BlockMemoryPoolTransactionCollector(
            settings,
            &chainstateRef_->getChainstateManager().CoinsTip(),
            chainstateRef_->getChainstateManager().ActiveChain(),
            chainstateRef_->getChainstateManager().GetBlockMap(),
            mempool,
            mainCS,
            relayTxFeeCalculator))
    , blockFactory_(
        BlockFactorySelector(
            settings,
            chainParameters,
            chainstateRef_->getChainstateManager().ActiveChain(),
            difficultyAdjuster,
            blockProofProver,
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
            mapHashedBlocks))
{
}

CoinMintingModule::~CoinMintingModule()
{
    coinMinter_.reset();
    blockFactory_.reset();
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