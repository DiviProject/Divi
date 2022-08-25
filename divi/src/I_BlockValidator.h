#ifndef I_BLOCK_VALIDATOR_H
#define I_BLOCK_VALIDATOR_H
#include <utility>
class CBlockIndex;
class CBlock;
class I_BlockValidator
{
public:
    virtual ~I_BlockValidator() {}
    virtual std::pair<CBlockIndex*, bool> validateAndAssignBlockIndex(CBlock& block, bool& blockChecked) const = 0;
    virtual bool connectActiveChain(CBlockIndex* blockIndex, const CBlock& block, bool& blockChecked) const = 0;
    virtual bool checkBlockRequirements(const CBlock& block, bool& checked) const = 0;
};
#endif// I_BLOCK_VALIDATOR_H