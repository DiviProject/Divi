#include <BlockDiskDataReader.h>

#include <BlockDiskAccessor.h>
#include <chain.h>
#include <BlockUndo.h>
#include <Logging.h>

bool BlockDiskDataReader::ReadBlock(const CBlockIndex* blockIndex, CBlock& block) const
{
    return ReadBlockFromDisk(block,blockIndex);
}
bool BlockDiskDataReader::ReadBlockUndo(const CBlockIndex* blockIndex, CBlockUndo& blockUndo) const
{
    CDiskBlockPos pos = blockIndex->GetUndoPos();
    if (pos.IsNull())
        return error("%s : no undo data available",__func__);
    if (!blockUndo.ReadFromDisk(pos, blockIndex->pprev->GetBlockHash()))
        return error("%s : failure reading undo data", __func__);

    return true;
}
