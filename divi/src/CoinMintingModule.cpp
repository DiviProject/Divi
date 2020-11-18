#include <CoinMintingModule.h>
#include <BlockFactory.h>
#include <CoinMinter.h>
#include <sync.h>
#include <ExtendedBlockFactory.h>
#include <chainparams.h>
#include <BlockMemoryPoolTransactionCollector.h>
#include <PoSTransactionCreator.h>

I_BlockFactory* BlockFactorySelector(
    I_BlockTransactionCollector& blockTransactionCollector,
    I_PoSTransactionCreator& coinstakeCreator,
    CWallet& wallet,
    int64_t& lastCoinStakeSearchInterval,
    BlockTimestampsByHeight& hashedBlockTimestampsByHeight,
    CChain& activeChain,
    const CChainParams& chainParameters,
    AnnotatedMixin<boost::recursive_mutex>& mainCS)
{
    if(chainParameters.NetworkID()==CBaseChainParams::Network::REGTEST)
    {
        return new ExtendedBlockFactory(
            blockTransactionCollector,
            coinstakeCreator,
            wallet,
            lastCoinStakeSearchInterval,
            hashedBlockTimestampsByHeight,
            activeChain,
            chainParameters,
            mainCS);
    }
    else
    {
        return new BlockFactory(
            blockTransactionCollector,
            coinstakeCreator,
            wallet,
            lastCoinStakeSearchInterval,
            hashedBlockTimestampsByHeight,
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
    CTxMemPool& mempool,
    std::vector<CNode*>& peers,
    CWallet& wallet,
    int64_t lastCoinStakeSearchInterval,
    BlockTimestampsByHeight hashedBlockTimestampsByHeight
    ): blockTransactionCollector_( new BlockMemoryPoolTransactionCollector(mempool,mainCS))
    , coinstakeTransactionCreator_( new PoSTransactionCreator(chainParameters,activeChain,wallet,lastCoinStakeSearchInterval,hashedBlockTimestampsByHeight))
    , blockFactory_(
        BlockFactorySelector(
            *blockTransactionCollector_,
            *coinstakeTransactionCreator_,
            wallet,
            lastCoinStakeSearchInterval,
            hashedBlockTimestampsByHeight,
            activeChain,
            chainParameters,
            mainCS))
    , coinMinter_( new CoinMinter(
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