#ifndef BLOCK_TEMPLATE_H
#define BLOCK_TEMPLATE_H

#include <primitives/block.h>
#include <stdint.h>
#include <amount.h>
#include <vector>
#include <memory>

class CBlockIndex;
class CMutableTransaction;
struct CBlockTemplate {
    std::shared_ptr<CMutableTransaction> coinbaseTransaction;
    const CBlockIndex* previousBlockIndex;
    CBlock block;
    std::vector<CAmount> vTxFees;
    std::vector<int64_t> vTxSigOps;
};
#endif // BLOCK_TEMPLATE_H