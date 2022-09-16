#ifndef BLOCK_TEMPLATE_H
#define BLOCK_TEMPLATE_H

#include <primitives/block.h>
#include <stdint.h>
#include <amount.h>
#include <vector>

class CBlockIndex;
struct CBlockTemplate {
    const CBlockIndex* previousBlockIndex;
    CBlock block;
};
#endif // BLOCK_TEMPLATE_H
