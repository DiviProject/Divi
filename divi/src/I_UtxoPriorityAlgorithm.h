#ifndef I_UTXO_PRIORITY_ALGORITHM_H
#define I_UTXO_PRIORITY_ALGORITHM_H
struct InputToSpendAndSigSize;
class I_UtxoPriorityAlgorithm
{
public:
    virtual ~I_UtxoPriorityAlgorithm(){}
    virtual bool operator()(const InputToSpendAndSigSize& first, const InputToSpendAndSigSize& second) const = 0;
};
#endif //I_UTXO_PRIORITY_ALGORITHM_H
