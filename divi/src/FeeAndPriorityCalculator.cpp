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

const CFeeRate& FeeAndPriorityCalculator::getFeeRateQuote() const
{
    return minimumRelayTransactionFee;
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

double FeeAndPriorityCalculator::ComputePriority(const CTransaction& tx, double dPriorityInputs, unsigned int nTxSize) const
{
    nTxSize = CalculateModifiedSize(tx, nTxSize);
    if (nTxSize == 0) return 0.0;

    return dPriorityInputs / nTxSize;
}

unsigned int FeeAndPriorityCalculator::CalculateModifiedSize(const CTransaction& tx, unsigned int nTxSize) const
{
    // In order to avoid disincentivizing cleaning up the UTXO set we don't count
    // the constant overhead for each txin and up to 110 bytes of scriptSig (which
    // is enough to cover a compressed pubkey p2sh redemption) for priority.
    // Providing any more cleanup incentive than making additional inputs free would
    // risk encouraging people to create junk outputs to redeem later.
    if (nTxSize == 0)
        nTxSize = tx.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);
    for (std::vector<CTxIn>::const_iterator it(tx.vin.begin()); it != tx.vin.end(); ++it)
    {
        unsigned int offset = 41U + std::min(110U, (unsigned int)it->scriptSig.size());
        if (nTxSize > offset)
            nTxSize -= offset;
    }
    return nTxSize;
}
