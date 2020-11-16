#include <CoinMintingModule.h>
#include <BlockFactory.h>
#include <CoinMinter.h>
#include <sync.h>

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
    ): blockFactory_(std::make_shared<BlockFactory>(
        wallet,
        lastCoinStakeSearchInterval,
        hashedBlockTimestampsByHeight,
        activeChain,
        chainParameters,
        mempool,
        mainCS))
    , coinMinter_(std::make_shared<CoinMinter>(
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

I_BlockFactory& CoinMintingModule::blockFactory() const
{
    return *blockFactory_;
}

I_CoinMinter& CoinMintingModule::coinMinter() const
{
    return *coinMinter_;
}