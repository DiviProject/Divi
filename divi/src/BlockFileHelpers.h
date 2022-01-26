#ifndef BLOCK_FILE_HELPERS_H
#define BLOCK_FILE_HELPERS_H
#include <vector>
#include <BlockFileInfo.h>
#include <set>

class CDiskBlockPos;
class CValidationState;
class CBlockTreeDB;
class CBlockIndex;
namespace BlockFileHelpers
{
    void FlushBlockFile(bool fFinalize = false);
    bool AllocateDiskSpaceForBlockUndo(
        int nFile,
        CDiskBlockPos& pos,
        unsigned int nAddSize);
    bool FindKnownBlockPos(
        CValidationState& state,
        CDiskBlockPos& pos,
        unsigned int nAddSize,
        unsigned int nHeight,
        uint64_t nTime);
    bool FindUnknownBlockPos(
        CValidationState& state,
        CDiskBlockPos& pos,
        unsigned int nAddSize,
        unsigned int nHeight,
        uint64_t nTime);
    bool WriteBlockFileToBlockTreeDatabase(
        CValidationState& state,
        CBlockTreeDB& blockTreeDB);
    void RecordDirtyBlockIndex(const CBlockIndex* blockIndexToRecord);
    void ReadBlockFiles(
        const CBlockTreeDB& blockTreeDB);
    int GetLastBlockHeightWrittenIntoLastBlockFile();
};
#endif// BLOCK_FILE_HELPERS_H
