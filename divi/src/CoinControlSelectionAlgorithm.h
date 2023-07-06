#ifndef COIN_CONTROL_COIN_SELECTOR_H
#define COIN_CONTROL_COIN_SELECTOR_H
#include <I_CoinSelectionAlgorithm.h>
class CCoinControl;
class CoinControlSelectionAlgorithm final: public I_CoinSelectionAlgorithm
{
private:
    const CCoinControl* coinControl_;
public:
    CoinControlSelectionAlgorithm(
        const CCoinControl* coinControl);
    bool isSelectable(const COutput& coin) const override;
    std::set<COutput> SelectCoins(
        const CMutableTransaction& transactionToSelectCoinsFor,
        const std::vector<COutput>& vCoins,
        CAmount& fees) const override;
};
#endif// COIN_CONTROL_COIN_SELECTOR_H