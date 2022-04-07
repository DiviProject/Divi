#ifndef COIN_MINTING_MODULE_H
#define COIN_MINTING_MODULE_H
#include <map>
#include <memory>
#include <vector>
#include <boost/thread/recursive_mutex.hpp>
class I_StakingWallet;
class CChainParams;
class CNode;
class CMasternodeSync;
class I_BlockFactory;
class CTxMemPool;
class CCriticalSection;
class I_CoinMinter;
class I_BlockTransactionCollector;
class I_PoSTransactionCreator;
class SuperblockSubsidyContainer;
class BlockIncentivesPopulator;
class CMasternodePayments;
class ProofOfStakeModule;
class CSporkManager;
class Settings;
class CCoinsViewCache;
class CFeeRate;
class MasternodeModule;
class I_PeerBlockNotifyService;

class CoinMintingModule
{
public:
    using LastExtensionTimestampByBlockHeight = std::map<unsigned int, unsigned int>;

private:
    class ChainstateManagerReference;
    LastExtensionTimestampByBlockHeight mapHashedBlocks_;
    std::unique_ptr<const ChainstateManagerReference> chainstate_;
    std::unique_ptr<ProofOfStakeModule> posModule_;
    std::unique_ptr<SuperblockSubsidyContainer> blockSubsidyContainer_;
    std::unique_ptr<BlockIncentivesPopulator> blockIncentivesPopulator_;
    std::unique_ptr<I_BlockTransactionCollector> blockTransactionCollector_;
    std::unique_ptr<I_PoSTransactionCreator> coinstakeTransactionCreator_;
    std::unique_ptr<I_BlockFactory> blockFactory_;
    std::unique_ptr<I_CoinMinter> coinMinter_;
public:
    CoinMintingModule(
        const Settings& settings,
        CCriticalSection& mainCS,
        const CChainParams& chainParameters,
        const MasternodeModule& masternodeModule,
        const CFeeRate& relayTxFeeCalculator,
        CTxMemPool& mempool,
        const I_PeerBlockNotifyService& peers,
        I_StakingWallet& wallet,
        const CSporkManager& sporkManager);
    ~CoinMintingModule();

    I_BlockFactory& blockFactory() const;
    I_CoinMinter& coinMinter() const;
    const LastExtensionTimestampByBlockHeight& GetBlockTimestampsByHeight() const;
};

#endif// COIN_MINTING_MODULE_H
