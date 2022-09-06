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

BlockSubmitter::BlockSubmitter(
    CCriticalSection& mainCriticalSection
    ): mainCriticalSection_(mainCriticalSection)
{
}

bool BlockSubmitter::IsBlockValidChainExtension(CBlock* pblock) const
{
    {
        LOCK(mainCriticalSection_);
        const ChainstateManager::Reference chainstate;
        if (pblock->hashPrevBlock != chainstate->ActiveChain().Tip()->GetBlockHash())
            return error("%s : generated block is stale",__func__);
    }
    return true;
}

bool BlockSubmitter::submitBlockForChainExtension(CBlock& block) const
{
    LogPrintf("%s\n", block);
    LogPrintf("generated %s\n", FormatMoney(block.vtx[0].vout[0].nValue));

    ChainstateManager::Reference chainstate;

    // Process this block the same as if we had received it from another node
    CValidationState state;
    if (!IsBlockValidChainExtension(&block) || !ProcessNewBlock(*chainstate, state, NULL, &block))
        return error("%s : block not accepted",__func__);

    return true;
}

bool BlockSubmitter::loadBlockForChainExtension(CValidationState& state, CBlock& block, CDiskBlockPos* blockfilePositionData) const
{
    ChainstateManager::Reference chainstate;
    return ProcessNewBlock(*chainstate, state, NULL, &block, blockfilePositionData);
}