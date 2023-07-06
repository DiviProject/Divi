#ifndef BLOCK_DISK_ACCESSOR_H
#define BLOCK_DISK_ACCESSOR_H
#include <stdint.h>
#include <I_BlockDataReader.h>
class CBlock;
struct CDiskBlockPos;
class CBlockIndex;

/** Functions for disk access for blocks */
bool CheckDiskSpace(uint64_t nAdditionalBytes = 0);
bool WriteBlockToDisk(const CBlock& block, CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex);
#endif // BLOCK_DISK_ACCESSOR_H
