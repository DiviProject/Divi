#ifndef MINIMUM_FEE_COIN_SELECTION_ALGORITHM_H
#define MINIMUM_FEE_COIN_SELECTION_ALGORITHM_H
#include <I_CoinSelectionAlgorithm.h>
class CKeyStore;
class I_SignatureSizeEstimator;
class MinimumFeeCoinSelectionAlgorithm: public I_CoinSelectionAlgorithm
{
private:
    const CKeyStore& keyStore_;
    const I_SignatureSizeEstimator& estimator_;
public:
    MinimumFeeCoinSelectionAlgorithm(
        const CKeyStore& keyStore,
        const I_SignatureSizeEstimator& estimator);
    virtual std::set<COutput> SelectCoins(
        const CMutableTransaction& transactionToSelectCoinsFor,
        const std::vector<COutput>& vCoins,
        CAmount& fees) const;
};
#endif// MINIMUM_FEE_COIN_SELECTION_ALGORITHM_H