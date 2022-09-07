#ifndef BLOCK_SUBMITTER_H
#define BLOCK_SUBMITTER_H
#include <I_BlockSubmitter.h>
class CValidationState;
class CDiskBlockPos;
class CCriticalSection;
class ChainstateManager;
class I_BlockValidator;
class BlockSubmitter final: public I_BlockSubmitter
{
private:
    const I_BlockValidator& blockValidator_;
    CCriticalSection& mainCriticalSection_;
    ChainstateManager& chainstate_;
    bool IsBlockValidChainExtension(CBlock* pblock) const;
public:
    BlockSubmitter(
        const I_BlockValidator& blockValidator,
        CCriticalSection& mainCriticalSection,
        ChainstateManager& chainstate);
    bool submitBlockForChainExtension(CBlock& block) const override;
    bool acceptBlockForChainExtension(CValidationState& state, CBlock& block, BlockDataSource blockDataSource) const override;
    bool loadBlockForChainExtension(
        CValidationState& state,
        CBlock& block,
        CDiskBlockPos* blockfilePositionData) const;
};
#endif// BLOCK_SUBMITTER_H