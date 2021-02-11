#ifndef BLOCK_DISK_ACCESSOR_H
#define BLOCK_DISK_ACCESSOR_H
#include <stdint.h>
class CBlock;
class CDiskBlockPos;
class CBlockIndex;
class uint256;
class CTransaction;
class COutPoint;

/** Functions for disk access for blocks */
bool WriteBlockToDisk(CBlock& block, CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex);
/** Get transaction from mempool or disk **/
bool GetTransaction(const uint256& hash, CTransaction& tx, uint256& hashBlock, bool fAllowSlow = false);
bool CollateralIsExpectedAmount(const COutPoint &outpoint, int64_t expectedAmount);
#endif // BLOCK_DISK_ACCESSOR_H