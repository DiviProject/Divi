#include <BlockDiskAccessor.h>

#include <BlockFileOpener.h>
#include <chain.h>
#include <streams.h>
#include <clientversion.h>
#include <chainparams.h>
#include <Logging.h>
#include <BlockUndo.h>

bool WriteBlockToDisk(CBlock& block, CDiskBlockPos& pos)
{
    // Open history file to append
    CAutoFile fileout(OpenBlockFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("WriteBlockToDisk : OpenBlockFile failed");

    // Write index header
    unsigned int nSize = fileout.GetSerializeSize(block);
    fileout << FLATDATA(Params().MessageStart()) << nSize;

    // Write block
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("WriteBlockToDisk : ftell failed");
    pos.nPos = (unsigned int)fileOutPos;
    fileout << block;

    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos)
{
    block.SetNull();

    // Open history file to read
    CAutoFile filein(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("ReadBlockFromDisk : OpenBlockFile failed");

    // Read block
    try {
        filein >> block;
    } catch (std::exception& e) {
        return error("%s : Deserialize or I/O error - %s", __func__, e.what());
    }

    // Check the header
    if (block.IsProofOfWork()) {
        if (!CheckProofOfWork(block.GetHash(), block.nBits, Params()))
            return error("ReadBlockFromDisk : Errors in block header");
    }

    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex)
{
    if (!ReadBlockFromDisk(block, pindex->GetBlockPos()))
        return false;
    if (block.GetHash() != pindex->GetBlockHash()) {
        LogPrintf("%s : block=%s index=%s\n", __func__, block.GetHash().ToString().c_str(), pindex->GetBlockHash().ToString().c_str());
        return error("ReadBlockFromDisk(CBlock&, CBlockIndex*) : GetHash() doesn't match index");
    }
    return true;
}

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