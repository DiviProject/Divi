#ifndef I_COIN_SELECTION_ALGORITHM_H
#define I_COIN_SELECTION_ALGORITHM_H
#include <vector>
#include <set>
#include <amount.h>
#include <Output.h>

class I_CoinSelectionAlgorithm
{
public:
    virtual ~I_CoinSelectionAlgorithm(){}
    virtual std::set<COutput> SelectCoins(
        const CAmount& nTargetValue,
        const std::vector<COutput>& vCoins) const = 0;
};
#endif// I_COIN_SELECTION_ALGORITHM_H