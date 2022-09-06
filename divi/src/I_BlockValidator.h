#ifndef I_BLOCK_VALIDATOR_H
#define I_BLOCK_VALIDATOR_H
#include <utility>
class CBlockIndex;
class CBlock;
class CValidationState;
class CDiskBlockPos;
class CNode;
struct NodeAndBlockDiskPosition
{
    CNode* dataSource;
    CDiskBlockPos* blockDiskPosition;
};
class I_BlockValidator
{
public:
    virtual ~I_BlockValidator() {}
    virtual bool connectActiveChain(const CBlock& block, CValidationState& state) const = 0;
    virtual bool checkBlockRequirements(const NodeAndBlockDiskPosition& nodeAndBlockDisk, const CBlock& block, CValidationState& state) const = 0;
    virtual std::pair<CBlockIndex*, bool> validateAndAssignBlockIndex(const NodeAndBlockDiskPosition& nodeAndBlockDisk, CBlock& block, CValidationState& state) const = 0;
};
#endif// I_BLOCK_VALIDATOR_H