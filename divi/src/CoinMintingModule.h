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
class I_BlockTransactionCollector;
class I_PoSTransactionCreator;
class SuperblockSubsidyContainer;
class BlockIncentivesPopulator;
class I_PoSStakeModifierService;
class BlockMap;
class CMasternodePayments;

class CoinMintingModule
{
    std::unique_ptr<I_PoSStakeModifierService> legacyStakeModifierService_;
    std::unique_ptr<I_PoSStakeModifierService> stakeModifierService_;
    std::unique_ptr<SuperblockSubsidyContainer> blockSubsidyContainer_;
    std::unique_ptr<BlockIncentivesPopulator> blockIncentivesPopulator_;
    std::unique_ptr<I_BlockTransactionCollector> blockTransactionCollector_;
    std::unique_ptr<I_PoSTransactionCreator> coinstakeTransactionCreator_;
    std::unique_ptr<I_BlockFactory> blockFactory_;
    std::unique_ptr<I_CoinMinter> coinMinter_;
public:
    CoinMintingModule(
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
        BlockMap& blockIndexByHash);
    ~CoinMintingModule();

    I_BlockFactory& blockFactory() const;
    I_CoinMinter& coinMinter() const;
};

#endif// COIN_MINTING_MODULE_H