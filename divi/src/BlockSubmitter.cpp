#include <BlockSubmitter.h>

#include <sync.h>
#include <ChainstateManager.h>
#include <primitives/block.h>
#include <main.h>
#include <Logging.h>
#include <utilmoneystr.h>
#include <spork.h>
#include <chain.h>
#include <ValidationState.h>
#include <I_BlockValidator.h>

BlockSubmitter::BlockSubmitter(
    const I_BlockValidator& blockValidator,
    CCriticalSection& mainCriticalSection,
    ChainstateManager& chainstate
    ): blockValidator_(blockValidator)
    , mainCriticalSection_(mainCriticalSection)
    , chainstate_(chainstate)
{
}

bool BlockSubmitter::IsBlockValidChainExtension(CBlock* pblock) const
{
    {
        LOCK(mainCriticalSection_);
        if (pblock->hashPrevBlock != chainstate_.ActiveChain().Tip()->GetBlockHash())
            return error("%s : generated block is stale",__func__);
    }
    return true;
}

bool BlockSubmitter::submitBlockForChainExtension(CBlock& block) const
{
    LogPrintf("%s\n", block);
    LogPrintf("generated %s\n", FormatMoney(block.vtx[0].vout[0].nValue));

    // Process this block the same as if we had received it from another node
    CValidationState state;
    if (!IsBlockValidChainExtension(&block) || !ProcessNewBlock(chainstate_, state, NULL, &block))
        return error("%s : block not accepted",__func__);

    return true;
}

bool BlockSubmitter::acceptBlockForChainExtension(CValidationState& state, CBlock& block, CNode* blockSourceNode) const
{
    return ProcessNewBlock(chainstate_, state, blockSourceNode, &block);
}

bool BlockSubmitter::loadBlockForChainExtension(CValidationState& state, CBlock& block, CDiskBlockPos* blockfilePositionData) const
{
    return ProcessNewBlock(chainstate_, state, NULL, &block, blockfilePositionData);
}