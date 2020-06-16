#ifndef COIN_MINTER_H
#define COIN_MINTER_H
#include <stdint.h>
class CWallet;

class CoinMinter
{
    CWallet* pwallet_;
    bool haveMintableCoins_;
    int64_t lastTimeCheckedMintable_;
    int64_t timeToWait_;
    static const int64_t constexpr fiveMinutes_ = 5 * 60;

    bool hasMintableCoinForProofOfStake();
public:
    CoinMinter(
        CWallet* pwallet
        );
    const int64_t& getTimeTillNextCheck() const;
    bool isReadyForProofOfStake();
};

#endif // COIN_MINTER_H