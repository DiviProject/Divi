#ifndef COIN_MINTING_MODULE_H
#define COIN_MINTING_MODULE_H
#include <map>
#include <memory>
#include <vector>
#include <boost/thread/recursive_mutex.hpp>
class I_StakingWallet;
class CChain;
class CChainParams;
class CNode;
class CMasternodeSync;
typedef std::map<unsigned int, unsigned int> BlockTimestampsByHeight;
class I_BlockFactory;
class CTxMemPool;
class CCriticalSection;
class BlockMap;
class I_CoinMinter;
class I_BlockTransactionCollector;
class I_PoSTransactionCreator;
class SuperblockSubsidyContainer;
class BlockIncentivesPopulator;
class BlockMap;
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
        const CChain& activeChain,
        const BlockMap& blockIndexByHash,
        const MasternodeModule& masternodeModule,
        const CFeeRate& relayTxFeeCalculator,
        CCoinsViewCache* baseCoinsViewCache,
        CTxMemPool& mempool,
        const I_PeerBlockNotifyService& peers,
        I_StakingWallet& wallet,
        BlockTimestampsByHeight& hashedBlockTimestampsByHeight,
        const CSporkManager& sporkManager);
    ~CoinMintingModule();

    I_BlockFactory& blockFactory() const;
    I_CoinMinter& coinMinter() const;
};

#endif// COIN_MINTING_MODULE_H
