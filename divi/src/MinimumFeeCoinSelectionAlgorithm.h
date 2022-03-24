#ifndef MINIMUM_FEE_COIN_SELECTION_ALGORITHM_H
#define MINIMUM_FEE_COIN_SELECTION_ALGORITHM_H
#include <I_CoinSelectionAlgorithm.h>
class CKeyStore;
class I_SignatureSizeEstimator;
class CFeeRate;
class MinimumFeeCoinSelectionAlgorithm: public I_CoinSelectionAlgorithm
{
private:
    const CKeyStore& keyStore_;
    const I_SignatureSizeEstimator& estimator_;
    const CFeeRate& minRelayTxFee_;
public:
    MinimumFeeCoinSelectionAlgorithm(
        const CKeyStore& keyStore,
        const I_SignatureSizeEstimator& estimator,
        const CFeeRate& minRelayTxFee);
    bool isSelectable(const COutput& coin) const override { return true; };
    std::set<COutput> SelectCoins(
        const CMutableTransaction& transactionToSelectCoinsFor,
        const std::vector<COutput>& vCoins,
        CAmount& fees) const override;
};
#endif// MINIMUM_FEE_COIN_SELECTION_ALGORITHM_H