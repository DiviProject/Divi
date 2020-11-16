#ifndef COIN_MINTING_MODULE_H
#define COIN_MINTING_MODULE_H
#include <map>
#include <memory>
#include <vector>
#include <boost/thread/recursive_mutex.hpp>
class CWallet;
class CChain;
class CChainParams;
class CNode;
class CMasternodeSync;
typedef std::map<unsigned int, unsigned int> BlockTimestampsByHeight;
class I_BlockFactory;
class CTxMemPool;
template <typename MutexObj>
class AnnotatedMixin;
class I_CoinMinter;

class CoinMintingModule
{
    std::shared_ptr<I_BlockFactory> blockFactory_;
    std::shared_ptr<I_CoinMinter> coinMinter_;
public:
    CoinMintingModule(
        AnnotatedMixin<boost::recursive_mutex>& mainCS,
        const CChainParams& chainParameters,
        CChain& activeChain,
        CMasternodeSync& masternodeSynchronization,
        CTxMemPool& mempool,
        std::vector<CNode*>& peers,
        CWallet& wallet,
        int64_t lastCoinStakeSearchInterval,
        BlockTimestampsByHeight hashedBlockTimestampsByHeight);

    I_BlockFactory& blockFactory() const;
    I_CoinMinter& coinMinter() const;
};

#endif// COIN_MINTING_MODULE_H