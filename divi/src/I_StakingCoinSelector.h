#ifndef I_STAKING_COIN_SELECTOR_H
#define I_STAKING_COIN_SELECTOR_H
#include <StakableCoin.h>
#include <set>
#include <keystore.h>
#include <I_KeypoolReserver.h>

class I_StakingCoinSelector
{
public:
    virtual ~I_StakingCoinSelector(){}
    virtual bool SelectStakeCoins(std::set<StakableCoin>&) const = 0;
    virtual bool HasAgedCoins() const = 0;
    virtual bool CanStakeCoins() const = 0;
};
class I_StakingWallet: public virtual CKeyStore, public I_StakingCoinSelector, public I_KeypoolReserver
{
public:
    virtual ~I_StakingWallet(){}
};
#endif// I_STAKING_COIN_SELECTOR_H