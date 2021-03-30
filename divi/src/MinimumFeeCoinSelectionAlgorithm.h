#ifndef MINIMUM_FEE_COIN_SELECTION_ALGORITHM_H
#define MINIMUM_FEE_COIN_SELECTION_ALGORITHM_H
#include <I_CoinSelectionAlgorithm.h>
class CKeyStore;
class MinimumFeeCoinSelectionAlgorithm: public I_CoinSelectionAlgorithm
{
private:
    const CKeyStore& keyStore_;
public:
    MinimumFeeCoinSelectionAlgorithm(const CKeyStore& keyStore);
    virtual std::set<COutput> SelectCoins(
        const CMutableTransaction& transactionToSelectCoinsFor,
        const std::vector<COutput>& vCoins,
        CAmount& fees) const;
};
#endif// MINIMUM_FEE_COIN_SELECTION_ALGORITHM_H