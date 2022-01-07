#ifndef BLOCK_FILE_HELPERS_H
#define BLOCK_FILE_HELPERS_H
#include <vector>
#include <BlockFileInfo.h>

namespace BlockFileHelpers
{
    void FlushBlockFile(int nLastBlockFile, const std::vector<CBlockFileInfo>& vinfoBlockFile, bool fFinalize = false);
};
#endif// BLOCK_FILE_HELPERS_H