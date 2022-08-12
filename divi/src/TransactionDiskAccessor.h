#ifndef TRANSACTION_DISK_ACCESSOR_H
#define TRANSACTION_DISK_ACCESSOR_H
#include <stdint.h>
class uint256;
class CTransaction;
class COutPoint;
class CTxMemPool;
class CCriticalSection;

/** Get transaction from mempool or disk **/
void InitializeTransactionDiskAccessors(CTxMemPool& mempool, CCriticalSection& mainCriticalSection);
bool GetTransaction(const uint256& hash, CTransaction& tx, uint256& hashBlock, bool fAllowSlow);
bool CollateralIsExpectedAmount(const COutPoint &outpoint, int64_t expectedAmount);
#endif // TRANSACTION_DISK_ACCESSOR_H