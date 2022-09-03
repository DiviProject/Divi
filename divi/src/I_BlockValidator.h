#ifndef I_BLOCK_VALIDATOR_H
#define I_BLOCK_VALIDATOR_H
#include <utility>
class CBlockIndex;
class CBlock;
class CValidationState;
class I_BlockValidator
{
public:
    virtual ~I_BlockValidator() {}
    virtual std::pair<CBlockIndex*, bool> validateAndAssignBlockIndex(CBlock& block, bool blockChecked, CValidationState& state) const = 0;
    virtual bool connectActiveChain(const CBlock& block, bool blockChecked, CValidationState& state) const = 0;
    virtual bool checkBlockRequirements(const CBlock& block, bool& checked, CValidationState& state) const = 0;
};
#endif// I_BLOCK_VALIDATOR_H