#include <BlockDiskAccessor.h>

#include <boost/filesystem.hpp>
#include <DataDirectory.h>
#include <BlockFileOpener.h>
#include <chain.h>
#include <streams.h>
#include <clientversion.h>
#include <chainparams.h>
#include <Logging.h>
#include <BlockUndo.h>

/** Check whether enough disk space is available for an incoming block */
bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    /** Minimum disk space required - used in CheckDiskSpace() */
    static const uint64_t nMinDiskSpace = 52428800;
    uint64_t nFreeBytesAvailable = boost::filesystem::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return false; // Should call AbortNode when this returns false;

    return true;
}

bool WriteBlockToDisk(const CBlock& block, CDiskBlockPos& pos)
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

    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex)
{
    if (!ReadBlockFromDisk(block, pindex->GetBlockPos()))
        return false;
    if (block.GetHash() != pindex->GetBlockHash()) {
        LogPrintf("%s : block=%s index=%s\n", __func__, block.GetHash(), pindex->GetBlockHash());
        return error("ReadBlockFromDisk(CBlock&, CBlockIndex*) : GetHash() doesn't match index");
    }
    return true;
}