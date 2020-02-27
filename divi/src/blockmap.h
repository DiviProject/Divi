#ifndef BLOCK_MAP_H
#define BLOCK_MAP_H
#include "chain.h"
#include <boost/unordered_map.hpp>
struct BlockHasher {
    size_t operator()(const uint256& hash) const { return hash.GetLow64(); }
};
class BlockMap: public boost::unordered_map<uint256, CBlockIndex*, BlockHasher>
{
};
#endif // BLOCK_MAP_H