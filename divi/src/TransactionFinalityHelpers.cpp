#include <TransactionFinalityHelpers.h>

#include <sync.h>
#include <primitives/transaction.h>
#include <chain.h>
#include <timedata.h>

bool IsFinalTx(CCriticalSection& mainCriticalSection, const CTransaction& tx, const CChain& activeChain, int nBlockHeight, int64_t nBlockTime)
{
    AssertLockHeld(mainCriticalSection);
    // Time based nLockTime implemented in 0.1.6
    if (tx.nLockTime == 0)
        return true;
    if (nBlockHeight == 0)
        nBlockHeight = activeChain.Height();
    if (nBlockTime == 0)
        nBlockTime = GetAdjustedTime();
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for(const CTxIn& txin: tx.vin)
    {
        if (!txin.IsFinal()) return false;
    }
    return true;
}