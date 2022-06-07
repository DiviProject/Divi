#ifndef STOCHASTIC_SUBSET_COIN_SELECTOR_H
#define STOCHASTIC_SUBSET_COIN_SELECTOR_H
#include <I_CoinSelectionAlgorithm.h>
#include <functional>
class CWalletTx;
typedef std::function<int(const CWalletTx&,int,int)> TxConfirmationChecker;
bool SelectCoinsMinConf(
    const TxConfirmationChecker& txConfirmationChecker,
    const CAmount& nTargetValue,
    int nConfMine,
    int nConfTheirs,
    std::vector<COutput> vCoins,
    std::set<COutput>& setCoinsRet,
    CAmount& nValueRet);
class StochasticSubsetSelectionAlgorithm: public I_CoinSelectionAlgorithm
{
private:
    TxConfirmationChecker txConfirmationChecker_;
    const bool& allowSpendingZeroConfirmationOutputs_;
public:
    StochasticSubsetSelectionAlgorithm(
        TxConfirmationChecker txConfirmationChecker,
        const bool& allowSpendingZeroConfirmationOutputs);
    bool isSelectable(const COutput& coin) const override { return true;}
    std::set<COutput> SelectCoins(
        const CMutableTransaction& transactionToSelectCoinsFor,
        const std::vector<COutput>& vCoins,
        CAmount& fees) const;
};

#endif// STOCHASTIC_SUBSET_COIN_SELECTOR_H