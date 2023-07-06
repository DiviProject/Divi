#ifndef I_COIN_MINTER_H
#define I_COIN_MINTER_H
#include <stdint.h>

class I_CoinMinter
{
public:
    virtual ~I_CoinMinter(){};
    virtual bool canMintCoins() = 0;
    virtual void sleep(uint64_t milliseconds) const = 0;
    virtual void setMintingRequestStatus(bool newStatus) = 0;
    virtual bool mintingHasBeenRequested() const = 0;

    virtual bool createNewBlock() const = 0;
};

#endif // I_COIN_MINTER_H
