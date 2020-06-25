#ifndef I_COIN_MINTER_H
#define I_COIN_MINTER_H
#include <stdint.h>
class CReserveKey;

class I_CoinMinter
{
public:
    virtual ~I_CoinMinter(){};
    virtual bool CanMintCoins() = 0;
    virtual bool satisfiesMintingRequirements() const = 0;
    virtual bool limitStakingSpeed() const = 0;
    virtual bool isAtProofOfStakeHeight() const = 0;
    virtual void sleep(uint64_t milliseconds) const = 0;
    virtual void setMintingRequestStatus(bool newStatus) = 0;
    virtual bool mintingHasBeenRequested() const = 0;

    virtual bool createNewBlock(
        unsigned int nExtraNonce, 
        CReserveKey& reserveKey, 
        bool fProofOfStake) const = 0;
};

#endif // I_COIN_MINTER_H