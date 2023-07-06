#include <BlockFileHelpers.h>

#include <sync.h>
#include <util.h>
#include <BlockDiskPosition.h>
#include <BlockFileOpener.h>
#include <defaultValues.h>
#include <BlockDiskAccessor.h>
#include <ValidationState.h>
#include <txdb.h>
#include <chain.h>
#include <set>
#include <Logging.h>

CCriticalSection cs_LastBlockFile;
/** Dirty block file entries. */
std::set<int> setDirtyFileInfo;
/** Dirty block index entries. */
std::set<const CBlockIndex*> setDirtyBlockIndex;
int nLastBlockFile = 0;
std::vector<CBlockFileInfo> vinfoBlockFile;

void BlockFileHelpers::FlushBlockFile(bool fFinalize)
{
    LOCK(cs_LastBlockFile);
    CDiskBlockPos posOld(nLastBlockFile, 0);

    FILE* fileOld = OpenBlockFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nUndoSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }
}

bool BlockFileHelpers::AllocateDiskSpaceForBlockUndo(
    int nFile,
    CDiskBlockPos& pos,
    unsigned int nAddSize)
{
    LOCK(cs_LastBlockFile);
    pos.nFile = nFile;

    unsigned int nNewSize;
    pos.nPos = vinfoBlockFile[nFile].nUndoSize;
    vinfoBlockFile[nFile].nUndoSize += nAddSize;
    nNewSize = vinfoBlockFile[nFile].nUndoSize;
    setDirtyFileInfo.insert(nFile);

    unsigned int nOldChunks = (pos.nPos + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    unsigned int nNewChunks = (nNewSize + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    if (nNewChunks > nOldChunks) {
        if (CheckDiskSpace(nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos)) {
            FILE* file = OpenUndoFile(pos);
            if (file) {
                LogPrintf("Pre-allocating up to position 0x%x in rev%05u.dat\n", nNewChunks * UNDOFILE_CHUNK_SIZE, pos.nFile);
                AllocateFileRange(file, pos.nPos, nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos);
                fclose(file);
            }
        }
        else
        {
            return false;
        }
    }

    return true;
}

bool BlockFileHelpers::FindKnownBlockPos(
    CValidationState& state,
    CDiskBlockPos& pos,
    unsigned int nAddSize,
    unsigned int nHeight,
    uint64_t nTime)
{
    LOCK(cs_LastBlockFile);
    unsigned int nFile = pos.nFile;
    if (vinfoBlockFile.size() <= nFile) {
        vinfoBlockFile.resize(nFile + 1);
    }

    nLastBlockFile = nFile;
    vinfoBlockFile[nFile].AddBlock(nHeight, nTime);
    vinfoBlockFile[nFile].nSize = std::max(pos.nPos + nAddSize, vinfoBlockFile[nFile].nSize);

    setDirtyFileInfo.insert(nFile);
    return true;

}

bool BlockFileHelpers::FindUnknownBlockPos(
    CValidationState& state,
    CDiskBlockPos& pos,
    unsigned int nAddSize,
    unsigned int nHeight,
    uint64_t nTime)
{
    LOCK(cs_LastBlockFile);
    unsigned int nFile = nLastBlockFile;
    if (vinfoBlockFile.size() <= nFile) {
        vinfoBlockFile.resize(nFile + 1);
    }

    while (vinfoBlockFile[nFile].nSize + nAddSize >= MAX_BLOCKFILE_SIZE) {
        LogPrintf("Leaving block file %i: %s\n", nFile, vinfoBlockFile[nFile]);
        BlockFileHelpers::FlushBlockFile(true);
        nFile++;
        if (vinfoBlockFile.size() <= nFile) {
            vinfoBlockFile.resize(nFile + 1);
        }
    }
    pos.nFile = nFile;
    pos.nPos = vinfoBlockFile[nFile].nSize;

    nLastBlockFile = nFile;
    vinfoBlockFile[nFile].AddBlock(nHeight, nTime);
    vinfoBlockFile[nFile].nSize += nAddSize;

    unsigned int nOldChunks = (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
    unsigned int nNewChunks = (vinfoBlockFile[nFile].nSize + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
    if (nNewChunks > nOldChunks) {
        if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos)) {
            FILE* file = OpenBlockFile(pos);
            if (file) {
                LogPrintf("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * BLOCKFILE_CHUNK_SIZE, pos.nFile);
                AllocateFileRange(file, pos.nPos, nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos);
                fclose(file);
            }
        }
        else
        {
            return state.Abort("Disk space is low!");
        }
    }
    setDirtyFileInfo.insert(nFile);
    return true;
}

bool BlockFileHelpers::WriteBlockFileToBlockTreeDatabase(
    CValidationState& state,
    CBlockTreeDB& blockTreeDB)
{
    BlockFileHelpers::FlushBlockFile(false);
    for (int fileInfoID: setDirtyFileInfo)
    {
        if (!blockTreeDB.WriteBlockFileInfo(fileInfoID, vinfoBlockFile[fileInfoID]))
        {
            return state.Abort("Failed to write to block index");
        }
    }
    setDirtyFileInfo.clear();
    if (!blockTreeDB.WriteLastBlockFile(nLastBlockFile))
    {
        return state.Abort("Failed to write to block index");
    }

    for (const auto* blockIndex: setDirtyBlockIndex)
    {
        if (!blockTreeDB.WriteBlockIndex(CDiskBlockIndex(blockIndex)))
        {
            return state.Abort("Failed to write to block index");
        }
    }
    setDirtyBlockIndex.clear();
    return true;
}

void BlockFileHelpers::RecordDirtyBlockIndex(const CBlockIndex* blockIndexToRecord)
{
    setDirtyBlockIndex.insert(blockIndexToRecord);
}

void BlockFileHelpers::ReadBlockFiles(
    const CBlockTreeDB& blockTreeDB)
{
    blockTreeDB.ReadLastBlockFile(nLastBlockFile);
    vinfoBlockFile.resize(nLastBlockFile + 1);
    LogPrintf("%s: last block file = %i\n", __func__, nLastBlockFile);
    for (int nFile = 0; nFile <= nLastBlockFile; nFile++) {
        blockTreeDB.ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
    }
    LogPrintf("%s: last block file info: %s\n", __func__, vinfoBlockFile[nLastBlockFile]);
    for (int nFile = nLastBlockFile + 1; true; nFile++) {
        CBlockFileInfo info;
        if (blockTreeDB.ReadBlockFileInfo(nFile, info)) {
            vinfoBlockFile.push_back(info);
        } else {
            break;
        }
    }
}

int BlockFileHelpers::GetLastBlockHeightWrittenIntoLastBlockFile()
{
    return vinfoBlockFile[nLastBlockFile].nHeightLast;
}
