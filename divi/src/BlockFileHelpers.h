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
    void FlushBlockFile(int nLastBlockFile, const std::vector<CBlockFileInfo>& vinfoBlockFile, bool fFinalize = false);
    bool AllocateDiskSpaceForBlockUndo(
        int nFile,
        std::vector<CBlockFileInfo>& vinfoBlockFile,
        CDiskBlockPos& pos,
        unsigned int nAddSize);
    bool FindKnownBlockPos(
        int& nLastBlockFile,
        std::vector<CBlockFileInfo>& vinfoBlockFile,
        CValidationState& state,
        CDiskBlockPos& pos,
        unsigned int nAddSize,
        unsigned int nHeight,
        uint64_t nTime);
    bool FindUnknownBlockPos(
        int& nLastBlockFile,
        std::vector<CBlockFileInfo>& vinfoBlockFile,
        CValidationState& state,
        CDiskBlockPos& pos,
        unsigned int nAddSize,
        unsigned int nHeight,
        uint64_t nTime);
    bool WriteBlockFileToBlockTreeDatabase(
        const int nLastBlockFile,
        std::set<CBlockIndex*>& setDirtyBlockIndex,
        const std::vector<CBlockFileInfo>& vinfoBlockFile,
        CValidationState& state,
        CBlockTreeDB& blockTreeDB);
};
#endif// BLOCK_FILE_HELPERS_H