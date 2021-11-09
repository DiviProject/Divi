#include <FeeAndPriorityCalculator.h>
#include "primitives/transaction.h"
#include "FeeRate.h"
#include <version.h>
#include <defaultValues.h>

FeeAndPriorityCalculator::FeeAndPriorityCalculator(): minimumRelayTransactionFee(DEFAULT_TX_RELAY_FEE_PER_KILOBYTE)
{
}

void FeeAndPriorityCalculator::setFeeRate(CAmount ratePerKB)
{
    minimumRelayTransactionFee = CFeeRate(ratePerKB);
}

const CFeeRate& FeeAndPriorityCalculator::getMinimumRelayFeeRate() const
{
    return minimumRelayTransactionFee;
}

void FeeAndPriorityCalculator::SetMaxFee(CAmount maximumFee)
{
    minimumRelayTransactionFee.SetMaxFee(maximumFee);
}

CAmount FeeAndPriorityCalculator::MinimumValueForNonDust() const
{
    return 3*minimumRelayTransactionFee.GetFee(182u);
}

CAmount FeeAndPriorityCalculator::MinimumValueForNonDust(const CTxOut& txout) const
{
    // "Dust" is defined in terms of CTransaction::minimumRelayTransactionFee, which has units duffs-per-kilobyte.
    // If you'd pay more than 1/3 in fees to spend something, then we consider it dust.
    // A typical txout is 34 bytes big, and will need a CTxIn of at least 148 bytes to spend
    // i.e. total is 148 + 32 = 182 bytes. Default -minimumRelayTransactionFee is 10000 duffs per kB
    // and that means that fee per txout is 182 * 10000 / 1000 = 1820 duffs.
    // So dust is a txout less than 1820 *3 = 5460 duffs
    // with default -minimumRelayTransactionFee = minimumRelayTransactionFee = 10000 duffs per kB.
    const size_t nSize = txout.GetSerializeSize(SER_DISK,0)+148u;
    return 3*minimumRelayTransactionFee.GetFee(nSize);
}
bool FeeAndPriorityCalculator::IsDust(const CTxOut& txout) const
{
    return txout.nValue < MinimumValueForNonDust(txout);
}
