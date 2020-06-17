#ifndef COIN_MINTER_H
#define COIN_MINTER_H
#include <stdint.h>
#include <memory>
#include <vector>

class CWallet;
class CChain;
class CChainParams;
class PeerNotificationOfMintService;
class CNode;
class CMasternodeSync;
class CoinMinter
{
    CWallet* pwallet_;
    CChain& chain_;
    const CChainParams& chainParameters_;
    std::shared_ptr<PeerNotificationOfMintService> peerNotifier_;
    CMasternodeSync& masternodeSync_;
    bool haveMintableCoins_;
    int64_t lastTimeCheckedMintable_;
    int64_t timeToWait_;
    static const int64_t constexpr fiveMinutes_ = 5 * 60;

    bool hasMintableCoinForProofOfStake();
public:
    CoinMinter(
        CWallet* pwallet,
        CChain& chain,
        const CChainParams& chainParameters,
        std::vector<CNode*>& peers,
        CMasternodeSync& masternodeSynchronization
        );
    const int64_t& getTimeTillNextCheck() const;
    bool isMintable();
    bool isAtProofOfStakeHeight() const;
};

#endif // COIN_MINTER_H