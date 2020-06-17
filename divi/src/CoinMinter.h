#ifndef COIN_MINTER_H
#define COIN_MINTER_H
#include <stdint.h>
class CWallet;
class CChain;
class CChainParams;

class CoinMinter
{
    CWallet* pwallet_;
    CChain& chain_;
    const CChainParams& chainParameters_;
    bool haveMintableCoins_;
    int64_t lastTimeCheckedMintable_;
    int64_t timeToWait_;
    static const int64_t constexpr fiveMinutes_ = 5 * 60;

    bool hasMintableCoinForProofOfStake();
public:
    CoinMinter(
        CWallet* pwallet,
        CChain& chain,
        const CChainParams& chainParameters
        );
    const int64_t& getTimeTillNextCheck() const;
    bool isMintable();
    bool isAtProofOfStakeHeight() const;
};

#endif // COIN_MINTER_H