#ifndef BLOCK_FILE_HELPERS_H
#define BLOCK_FILE_HELPERS_H
#include <vector>
#include <set>
#include <BlockFileInfo.h>

class CDiskBlockPos;
namespace BlockFileHelpers
{
    void FlushBlockFile(int nLastBlockFile, const std::vector<CBlockFileInfo>& vinfoBlockFile, bool fFinalize = false);
    bool AllocateDiskSpaceForBlockUndo(
        int nFile,
        std::set<int>& setDirtyFileInfo,
        std::vector<CBlockFileInfo>& vinfoBlockFile,
        CDiskBlockPos& pos,
        unsigned int nAddSize);
};
#endif// BLOCK_FILE_HELPERS_H