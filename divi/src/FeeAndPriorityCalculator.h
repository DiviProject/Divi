#ifndef FEE_AND_PRIORITY_CALCULATOR
#define FEE_AND_PRIORITY_CALCULATOR

#include <amount.h>
class CTxOut;
class CTransaction;
class CFeeRate;

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
    const CFeeRate& getFeeRateQuote() const;
    CAmount MinimumValueForNonDust() const;
    CAmount MinimumValueForNonDust(const CTxOut& txout) const;
    bool IsDust(const CTxOut& txout) const;
    double ComputePriority(const CTransaction& tx, double dPriorityInputs, unsigned int nTxSize = 0) const;
    unsigned int CalculateModifiedSize(const CTransaction& tx, unsigned int nTxSize = 0) const;
};

#endif //FEE_AND_PRIORITY_CALCULATOR