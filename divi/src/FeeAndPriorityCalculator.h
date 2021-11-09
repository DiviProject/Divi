#ifndef FEE_AND_PRIORITY_CALCULATOR
#define FEE_AND_PRIORITY_CALCULATOR

#include <amount.h>
#include <FeeRate.h>
class CTxOut;
class CTransaction;
class CFeeRate;

class FeeAndPriorityCalculator
{
private:
    CFeeRate minimumRelayTransactionFee;
    FeeAndPriorityCalculator();

public:
    static FeeAndPriorityCalculator& instance()
    {
        static FeeAndPriorityCalculator uniqueInstance;
        return uniqueInstance;
    }

    void setFeeRate(CAmount ratePerKB);
    const CFeeRate& getMinimumRelayFeeRate() const;

    void SetMaxFee(CAmount maximumFee);
    CAmount MinimumValueForNonDust() const;
    CAmount MinimumValueForNonDust(const CTxOut& txout) const;
    bool IsDust(const CTxOut& txout) const;
    double ComputeInputCoinAgePerByte(const CTransaction& tx, double dPriorityInputs, unsigned int nTxSize = 0) const;
    unsigned int CalculateModifiedSize(const CTransaction& tx, unsigned int nTxSize = 0) const;
};

#endif //FEE_AND_PRIORITY_CALCULATOR