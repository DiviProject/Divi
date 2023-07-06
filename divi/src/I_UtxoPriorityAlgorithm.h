#ifndef I_UTXO_PRIORITY_ALGORITHM_H
#define I_UTXO_PRIORITY_ALGORITHM_H
#include <amount.h>
struct InputToSpendAndSigSize;
class I_UtxoPriorityAlgorithm
{
public:
    virtual ~I_UtxoPriorityAlgorithm(){}
    virtual bool operator()(const CAmount targetAmount, const InputToSpendAndSigSize& first, const InputToSpendAndSigSize& second) const = 0;
};
#endif //I_UTXO_PRIORITY_ALGORITHM_H
