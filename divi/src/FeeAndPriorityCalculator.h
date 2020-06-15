#ifndef FEE_AND_PRIORITY_CALCULATOR
#define FEE_AND_PRIORITY_CALCULATOR

class CTxOut;
class CTransaction;

class FeeAndPriorityCalculator
{
private:
    FeeAndPriorityCalculator();
public:
    static const FeeAndPriorityCalculator& instance()
    {
        static FeeAndPriorityCalculator uniqueInstance;
        return uniqueInstance;
    }
    bool IsDust(const CTxOut& txout) const;
    double ComputePriority(const CTransaction& tx, double dPriorityInputs, unsigned int nTxSize = 0) const;
    unsigned int CalculateModifiedSize(const CTransaction& tx, unsigned int nTxSize = 0) const;
};

#endif //FEE_AND_PRIORITY_CALCULATOR