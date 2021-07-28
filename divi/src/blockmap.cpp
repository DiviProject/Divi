#include <blockmap.h>

CBlockIndex* BlockMap::GetUniqueBlockIndexForHash(uint256 blockHash)
{
    if (blockHash == 0)
        return NULL;

    // Return existing
    BlockMap::iterator mi = find(blockHash);
    if (mi != end())
        return (*mi).second;

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    if (!pindexNew)
        throw std::runtime_error("LoadBlockIndex() : new CBlockIndex failed");
    mi = insert(std::make_pair(blockHash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);
    return pindexNew;
}