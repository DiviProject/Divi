#ifndef TRANSACTION_FINALITY_HELPERS_H
#define TRANSACTION_FINALITY_HELPERS_H
#include <stdint.h>
class CCriticalSection;
class CTransaction;
class CChain;
bool IsFinalTx(CCriticalSection& mainCriticalSection, const CTransaction& tx, const CChain& activeChain, int nBlockHeight = 0 , int64_t nBlockTime = 0);
#endif// TRANSACTION_FINALITY_HELPERS_H