#ifndef BLOCK_SUBMITTER_H
#define BLOCK_SUBMITTER_H
#include <I_BlockSubmitter.h>
class CValidationState;
class CDiskBlockPos;
class CCriticalSection;
class ChainstateManager;
class BlockSubmitter final: public I_BlockSubmitter
{
private:
    CCriticalSection& mainCriticalSection_;
    ChainstateManager& chainstate_;
    bool IsBlockValidChainExtension(CBlock* pblock) const;
public:
    BlockSubmitter(
        CCriticalSection& mainCriticalSection,
        ChainstateManager& chainstate);
    bool submitBlockForChainExtension(CBlock& block) const override;
    bool acceptBlockForChainExtension(CValidationState& state, CBlock& block, CNode* blockSourceNode) const override;
    bool loadBlockForChainExtension(
        CValidationState& state,
        CBlock& block,
        CDiskBlockPos* blockfilePositionData) const;
};
#endif// BLOCK_SUBMITTER_H