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

CCriticalSection cs_LastBlockFile;
/** Dirty block file entries. */
std::set<int> setDirtyFileInfo;

void BlockFileHelpers::FlushBlockFile(int nLastBlockFile, const std::vector<CBlockFileInfo>& vinfoBlockFile, bool fFinalize)
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
    std::vector<CBlockFileInfo>& vinfoBlockFile,
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
    int& nLastBlockFile,
    std::vector<CBlockFileInfo>& vinfoBlockFile,
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
    int& nLastBlockFile,
    std::vector<CBlockFileInfo>& vinfoBlockFile,
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
        BlockFileHelpers::FlushBlockFile(nLastBlockFile, vinfoBlockFile,true);
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
    const int nLastBlockFile,
    std::set<CBlockIndex*>& setDirtyBlockIndex,
    const std::vector<CBlockFileInfo>& vinfoBlockFile,
    CValidationState& state,
    CBlockTreeDB& blockTreeDB)
{
    BlockFileHelpers::FlushBlockFile(nLastBlockFile,vinfoBlockFile,false);
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

    for (CBlockIndex* blockIndex: setDirtyBlockIndex)
    {
        if (!blockTreeDB.WriteBlockIndex(CDiskBlockIndex(blockIndex)))
        {
            return state.Abort("Failed to write to block index");
        }
    }
    setDirtyBlockIndex.clear();
    return true;
}

void BlockFileHelpers::RecordDirtyBlockIndex(std::set<CBlockIndex*>& setDirtyBlockIndex,CBlockIndex* blockIndexToRecord)
{
    setDirtyBlockIndex.insert(blockIndexToRecord);
}