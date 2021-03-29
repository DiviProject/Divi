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
    const bool& allowSpendingZeroConfirmationOutputs_;
public:
    StochasticSubsetSelectionAlgorithm(
        TxConfirmationChecker txConfirmationChecker,
        const bool& allowSpendingZeroConfirmationOutputs);
    virtual std::set<COutput> SelectCoins(
        const CMutableTransaction& transactionToSelectCoinsFor,
        const CAmount& nTargetValue,
        const std::vector<COutput>& vCoins) const;
};

#endif// STOCHASTIC_SUBSET_COIN_SELECTOR_H