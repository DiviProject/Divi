#ifndef STOCHASTIC_SUBSET_COIN_SELECTOR_H
#define STOCHASTIC_SUBSET_COIN_SELECTOR_H
#include <I_CoinSelectionAlgorithm.h>
#include <functional>
class CWalletTx;
typedef std::function<int(const CWalletTx&,int,int)> TxConfirmationChecker;
class StochasticSubsetSelectionAlgorithm: public I_CoinSelectionAlgorithm
{
private:
    TxConfirmationChecker txConfirmationChecker_;
    bool allowSpendingZeroConfirmationOutputs_;
public:
    StochasticSubsetSelectionAlgorithm(
        TxConfirmationChecker txConfirmationChecker,
        bool allowSpendingZeroConfirmationOutputs);
    virtual std::set<COutput> SelectCoins(
            const CAmount& nTargetValue,
            const std::vector<COutput>& vCoins) const;
};

#endif// STOCHASTIC_SUBSET_COIN_SELECTOR_H