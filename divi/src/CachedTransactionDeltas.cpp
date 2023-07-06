#include <CachedTransactionDeltas.h>

CachedTransactionDeltas::CachedTransactionDeltas(
    ): credit(0)
    , debit(0)
    , changeAmount(0)
    , fees(0)
{
}
CachedTransactionDeltas::CachedTransactionDeltas(
    CAmount creditIn,
    CAmount debitIn,
    CAmount changeAmountIn,
    CAmount feesIn
    ): credit(creditIn)
    , debit(debitIn)
    , changeAmount(changeAmountIn)
    , fees(feesIn)
{
}