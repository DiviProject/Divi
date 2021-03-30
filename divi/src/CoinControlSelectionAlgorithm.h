#ifndef COIN_CONTROL_COIN_SELECTOR_H
#define COIN_CONTROL_COIN_SELECTOR_H
#include <I_CoinSelectionAlgorithm.h>
class CCoinControl;
class CoinControlSelectionAlgorithm: public I_CoinSelectionAlgorithm
{
private:
    const CCoinControl* coinControl_;
public:
    CoinControlSelectionAlgorithm(
        const CCoinControl* coinControl);
    virtual std::set<COutput> SelectCoins(
        const CMutableTransaction& transactionToSelectCoinsFor,
        const std::vector<COutput>& vCoins,
        CAmount& fees) const;
};
#endif// COIN_CONTROL_COIN_SELECTOR_H