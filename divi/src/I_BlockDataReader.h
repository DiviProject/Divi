#ifndef BLOCK_DATA_READER_H
#define BLOCK_DATA_READER_H
class CBlock;
class CBlockIndex;
class CBlockUndo;
class I_BlockDataReader
{
public:
    virtual ~I_BlockDataReader(){}
    virtual bool ReadBlock(const CBlockIndex* blockIndex, CBlock& block) const = 0;
    virtual bool ReadBlockUndo(const CBlockIndex* blockIndex, CBlockUndo& blockUndo) const = 0;
};
#endif// BLOCK_DATA_READER_H