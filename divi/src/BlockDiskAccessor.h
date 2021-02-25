#ifndef BLOCK_DISK_ACCESSOR_H
#define BLOCK_DISK_ACCESSOR_H
#include <stdint.h>
class CBlock;
class CDiskBlockPos;
class CBlockIndex;

/** Functions for disk access for blocks */
bool WriteBlockToDisk(CBlock& block, CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex);
#endif // BLOCK_DISK_ACCESSOR_H