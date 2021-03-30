#ifndef I_COIN_SELECTION_ALGORITHM_H
#define I_COIN_SELECTION_ALGORITHM_H
#include <vector>
#include <set>
#include <amount.h>
#include <Output.h>

struct CMutableTransaction;
class I_CoinSelectionAlgorithm
{
public:
    virtual ~I_CoinSelectionAlgorithm(){}
    virtual std::set<COutput> SelectCoins(
        const CMutableTransaction& transactionToSelectCoinsFor,
        const std::vector<COutput>& vCoins,
        CAmount& fees) const = 0;
};
#endif// I_COIN_SELECTION_ALGORITHM_H