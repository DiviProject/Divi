#ifndef BLOCK_SUBMITTER_H
#define BLOCK_SUBMITTER_H
#include <I_BlockSubmitter.h>
class CValidationState;
class CDiskBlockPos;
class BlockSubmitter final: public I_BlockSubmitter
{
private:
    bool IsBlockValidChainExtension(CBlock* pblock) const;
public:
    BlockSubmitter();
    bool submitBlockForChainExtension(CBlock& block) const override;
    bool loadBlockForChainExtension(
        CValidationState& state,
        CBlock& block,
        CDiskBlockPos* blockfilePositionData) const;
};
#endif// BLOCK_SUBMITTER_H