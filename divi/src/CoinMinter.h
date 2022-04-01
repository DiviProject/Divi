#ifndef COIN_MINTER_H
#define COIN_MINTER_H
#include <stdint.h>
#include <memory>
#include <vector>
#include <map>

#include <I_CoinMinter.h>

#include <boost/thread/recursive_mutex.hpp>

class I_StakingWallet;
class CChain;
class CChainParams;
class I_PeerBlockNotifyService;
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
class I_BlockSubsidyProvider;

class CoinMinter final: public I_CoinMinter
{
    static constexpr int64_t fiveMinutes_ = 5 * 60;
    const CChain& chain_;
    const CChainParams& chainParameters_;
    const I_PeerBlockNotifyService& peerNotifier_;
    const CMasternodeSync& masternodeSync_;

    I_BlockFactory& blockFactory_;
    I_StakingWallet& wallet_;
    HashedBlockMap& mapHashedBlocks_;

    bool mintingIsRequested_;
    mutable bool haveMintableCoins_;
    mutable int64_t lastTimeCheckedMintable_;
    mutable int64_t timeToWait_;

    bool hasMintableCoinForProofOfStake() const;
    bool limitStakingSpeed() const;
    bool nextBlockIsProofOfStake() const;

    bool ProcessBlockFound(CBlock* block, CReserveKey* reservekey) const;

    bool createProofOfStakeBlock() const;
    bool createProofOfWorkBlock() const;

public:
    CoinMinter(
        const CChain& chain,
        const CChainParams& chainParameters,
        const I_PeerBlockNotifyService& peers,
        const CMasternodeSync& masternodeSynchronization,
        I_BlockFactory& blockFactory,
        I_StakingWallet& wallet,
        HashedBlockMap& mapHashedBlocks);

    virtual bool CanMintCoins() override;
    virtual void sleep(uint64_t milliseconds) const override;
    virtual void setMintingRequestStatus(bool newStatus) override;
    virtual bool mintingHasBeenRequested() const override;

    bool createNewBlock() const override;
};

#endif // COIN_MINTER_H
