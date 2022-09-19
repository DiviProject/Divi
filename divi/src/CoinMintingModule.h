#ifndef COIN_MINTING_MODULE_H
#define COIN_MINTING_MODULE_H
#include <map>
#include <memory>
#include <vector>
#include <boost/thread/recursive_mutex.hpp>
class I_BlockFactory;
class I_BlockTransactionCollector;
class I_BlockSubmitter;
class I_BlockProofProver;
class I_CoinMinter;
class I_PoSTransactionCreator;
class I_PeerBlockNotifyService;
class I_ProofOfStakeGenerator;
class I_DifficultyAdjuster;
class I_StakingWallet;
class I_BlockProofProver;

class CChainParams;
class CMasternodeSync;
class CTxMemPool;
class CCriticalSection;
class CSporkManager;
class Settings;
class CFeeRate;
class ChainstateManagerReference;

class CoinMintingModule
{
private:
    std::unique_ptr<const ChainstateManagerReference> chainstateRef_;
    std::unique_ptr<I_BlockTransactionCollector> blockTransactionCollector_;
    std::unique_ptr<I_BlockFactory> blockFactory_;
    std::unique_ptr<I_CoinMinter> coinMinter_;

public:
    CoinMintingModule(
        const Settings& settings,
        const CChainParams& chainParameters,
        const I_BlockProofProver& blockProofProver,
        const CMasternodeSync& masternodeSynchronization,
        const CFeeRate& relayTxFeeCalculator,
        const I_PeerBlockNotifyService& peers,
        const I_BlockSubmitter& blockSubmitter,
        const I_DifficultyAdjuster& difficultyAdjuster,
        std::map<unsigned int, unsigned int>& mapHashedBlocks,
        CCriticalSection& mainCS,
        CTxMemPool& mempool,
        I_StakingWallet& wallet);
    ~CoinMintingModule();

    I_BlockFactory& blockFactory() const;
    I_CoinMinter& coinMinter() const;
};

#endif// COIN_MINTING_MODULE_H
