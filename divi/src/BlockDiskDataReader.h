#ifndef BLOCK_DISK_DATA_READER_H
#define BLOCK_DISK_DATA_READER_H
#include <I_BlockDataReader.h>
class CBlockIndex;
class CBlock;
class CBlockUndo;
class BlockDiskDataReader: public I_BlockDataReader
{
public:
    BlockDiskDataReader(){}
    bool ReadBlock(const CBlockIndex* blockIndex, CBlock& block) const override;
    bool ReadBlockUndo(const CBlockIndex* blockIndex, CBlockUndo& blockUndo) const override;
};
#endif// BLOCK_DISK_DATA_READER_H