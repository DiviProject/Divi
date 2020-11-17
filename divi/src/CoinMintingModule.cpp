#include <CoinMintingModule.h>
#include <BlockFactory.h>
#include <CoinMinter.h>
#include <sync.h>
#include <ExtendedBlockFactory.h>
#include <chainparams.h>

I_BlockFactory* BlockFactorySelector(
    CWallet& wallet,
    int64_t& lastCoinStakeSearchInterval,
    BlockTimestampsByHeight& hashedBlockTimestampsByHeight,
    CChain& activeChain,
    const CChainParams& chainParameters,
    CTxMemPool& mempool,
    AnnotatedMixin<boost::recursive_mutex>& mainCS)
{
    if(chainParameters.NetworkID()==CBaseChainParams::Network::REGTEST)
    {
        return new ExtendedBlockFactory(
            wallet,
            lastCoinStakeSearchInterval,
            hashedBlockTimestampsByHeight,
            activeChain,
            chainParameters,
            mempool,
            mainCS);
    }
    else
    {
        return new BlockFactory(
            wallet,
            lastCoinStakeSearchInterval,
            hashedBlockTimestampsByHeight,
            activeChain,
            chainParameters,
            mempool,
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
    ): blockFactory_(
        BlockFactorySelector(
            wallet,
            lastCoinStakeSearchInterval,
            hashedBlockTimestampsByHeight,
            activeChain,
            chainParameters,
            mempool,
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
}
I_BlockFactory& CoinMintingModule::blockFactory() const
{
    return *blockFactory_;
}

I_CoinMinter& CoinMintingModule::coinMinter() const
{
    return *coinMinter_;
}