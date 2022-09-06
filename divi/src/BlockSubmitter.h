#ifndef BLOCK_SUBMITTER_H
#define BLOCK_SUBMITTER_H
#include <I_BlockSubmitter.h>
class CValidationState;
class CDiskBlockPos;
class CCriticalSection;
class BlockSubmitter final: public I_BlockSubmitter
{
private:
    CCriticalSection& mainCriticalSection_;
    bool IsBlockValidChainExtension(CBlock* pblock) const;
public:
    BlockSubmitter(CCriticalSection& mainCriticalSection);
    bool submitBlockForChainExtension(CBlock& block) const override;
    bool loadBlockForChainExtension(
        CValidationState& state,
        CBlock& block,
        CDiskBlockPos* blockfilePositionData) const;
};
#endif// BLOCK_SUBMITTER_H