#ifndef I_BLOCK_SUBMITTED_H
#define I_BLOCK_SUBMITTED_H
class CBlock;
class CNode;
class CValidationState;
class I_BlockSubmitter
{
public:
    virtual ~I_BlockSubmitter(){}
    virtual bool submitBlockForChainExtension(CBlock& block) const = 0;
    virtual bool acceptBlockForChainExtension(CValidationState& state, CBlock& block, CNode* blockSourceNode) const = 0;
};
#endif// I_BLOCK_SUBMITTED_H