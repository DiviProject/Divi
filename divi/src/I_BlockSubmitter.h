#ifndef I_BLOCK_SUBMITTED_H
#define I_BLOCK_SUBMITTED_H
#include <boost/variant.hpp>
class CBlock;
class CNode;
class CValidationState;
class CDiskBlockPos;
typedef boost::variant<CNode*,CDiskBlockPos*> BlockDataSource;
class I_BlockSubmitter
{
public:
    virtual ~I_BlockSubmitter(){}
    virtual bool submitBlockForChainExtension(CBlock& block) const = 0;
    virtual bool acceptBlockForChainExtension(CValidationState& state, CBlock& block, BlockDataSource blockDataSource) const = 0;
};
#endif// I_BLOCK_SUBMITTED_H