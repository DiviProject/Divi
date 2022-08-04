#ifndef CACHED_TRANSACTION_DELTAS_H
#define CACHED_TRANSACTION_DELTAS_H
#include <amount.h>
struct CachedTransactionDeltas
{
    CAmount credit;
    CAmount debit;
    CAmount changeAmount;
    CAmount fees;
    CachedTransactionDeltas();
    CachedTransactionDeltas(
        CAmount creditIn,
        CAmount debitIn,
        CAmount changeAmountIn,
        CAmount feesIn);

    CachedTransactionDeltas& operator +=(const CachedTransactionDeltas& other)
    {
        credit += other.credit;
        debit += other.debit;
        changeAmount += other.changeAmount;
        fees += other.fees;
        return *this;
    }
};
#endif// CACHED_TRANSACTION_DELTAS_H