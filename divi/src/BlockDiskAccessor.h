#ifndef BLOCK_DISK_ACCESSOR_H
#define BLOCK_DISK_ACCESSOR_H
#include <stdint.h>
#include <I_BlockDataReader.h>
class CBlock;
class CDiskBlockPos;
class CBlockIndex;

/** Functions for disk access for blocks */
bool WriteBlockToDisk(CBlock& block, CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex);

class BlockDiskDataReader: public I_BlockDataReader
{
public:
    BlockDiskDataReader(){}
    virtual bool ReadBlock(const CBlockIndex* blockIndex, CBlock& block) const;
    virtual bool ReadBlockUndo(const CBlockIndex* blockIndex, CBlockUndo& blockUndo) const;
};
#endif // BLOCK_DISK_ACCESSOR_H