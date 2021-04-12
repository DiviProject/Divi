#ifndef COIN_MINTER_H
#define COIN_MINTER_H
#include <stdint.h>
#include <memory>
#include <vector>
#include <map>

#include <I_CoinMinter.h>

#include <boost/thread/recursive_mutex.hpp>

class CWallet;
class CChain;
class CChainParams;
class PeerNotificationOfMintService;
class CNode;
class CMasternodeSync;
class CReserveKey;
typedef std::map<unsigned int, unsigned int> HashedBlockMap;
class CBlock;
class CBlockIndex;
class CBlockHeader;
class I_BlockFactory;
class CBlockTemplate;
class CTxMemPool;
template <typename MutexObj>
class AnnotatedMixin;
class I_BlockSubsidyProvider;

class CoinMinter: public I_CoinMinter
{
    static constexpr int64_t fiveMinutes_ = 5 * 60;
    const I_BlockSubsidyProvider& blockSubsidies_;
    I_BlockFactory& blockFactory_;
    std::shared_ptr<PeerNotificationOfMintService> peerNotifier_;

    bool mintingIsRequested_;
    CWallet* pwallet_;
    const CChain& chain_;
    const CChainParams& chainParameters_;
    CTxMemPool& mempool_;
    AnnotatedMixin<boost::recursive_mutex>& mainCS_;
    const CMasternodeSync& masternodeSync_;
    HashedBlockMap& mapHashedBlocks_;
    bool haveMintableCoins_;
    int64_t lastTimeCheckedMintable_;
    int64_t timeToWait_;

    bool hasMintableCoinForProofOfStake();
    bool satisfiesMintingRequirements() const;
    bool limitStakingSpeed() const;
    bool nextBlockIsProofOfStake() const;

    bool ProcessBlockFound(CBlock* block, CReserveKey& reservekey) const;
    void IncrementExtraNonce(CBlock* block, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce) const;
    void UpdateTime(CBlockHeader* block, const CBlockIndex* pindexPrev) const;

    void SetBlockHeaders(
        CBlockTemplate& pblocktemplate,
        const bool& proofOfStake) const;
    void SetCoinbaseRewardAndHeight (
        CBlockTemplate& pblocktemplate,
        const bool& fProofOfStake) const;

    bool createProofOfStakeBlock(
        unsigned int nExtraNonce,
        CReserveKey& reserveKey) const;
    bool createProofOfWorkBlock(
        unsigned int nExtraNonce,
        CReserveKey& reserveKey) const;

public:
    CoinMinter(
        const I_BlockSubsidyProvider& blockSubsidies,
        I_BlockFactory& blockFactory,
        CWallet* pwallet,
        const CChain& chain,
        const CChainParams& chainParameters,
        std::vector<CNode*>& peers,
        const CMasternodeSync& masternodeSynchronization,
        HashedBlockMap& mapHashedBlocks,
        CTxMemPool& mempool,
        AnnotatedMixin<boost::recursive_mutex>& mainCS);

    virtual bool CanMintCoins();
    virtual void sleep(uint64_t milliseconds) const;
    virtual void setMintingRequestStatus(bool newStatus);
    virtual bool mintingHasBeenRequested() const;

    bool createNewBlock(
        unsigned int nExtraNonce,
        bool fProofOfStake) const override;
};

#endif // COIN_MINTER_H
