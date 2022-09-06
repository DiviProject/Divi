#include <AcceptBlockValidator.h>

#include <sync.h>
#include <BlockSigning.h>
#include <primitives/block.h>
#include <Logging.h>
#include <NodeStateRegistry.h>
#include <map>
#include <NodeId.h>
#include <chain.h>
#include <BlockCheckingHelpers.h>
#include <ChainstateManager.h>
#include <blockmap.h>
#include <Node.h>
#include <chainparams.h>

AcceptBlockValidator::AcceptBlockValidator(
        const I_ChainExtensionService& chainExtensionService,
        CCriticalSection& mainCriticalSection,
        const CChainParams& chainParameters,
        ChainstateManager& chainstate,
        CNode* pfrom,
        CDiskBlockPos* dbp
        ): chainExtensionService_(chainExtensionService)
        , mainCriticalSection_(mainCriticalSection)
        , chainParameters_(chainParameters)
        , chainstate_(chainstate)
        , pfrom_(pfrom)
        , dbp_(dbp)
{
}

std::pair<CBlockIndex*, bool> AcceptBlockValidator::validateAndAssignBlockIndex(CBlock& block, CValidationState& state) const
{
    LOCK(mainCriticalSection_);   // Replaces the former TRY_LOCK loop because busy waiting wastes too much resources
    MarkBlockAsReceived(block.GetHash());
    // Store to disk
    std::pair<CBlockIndex*,bool> assignmentResult = chainExtensionService_.assignBlockIndex(block, state, dbp_);
    if (assignmentResult.first && pfrom_) {
        chainExtensionService_.recordBlockSource(assignmentResult.first->GetBlockHash(), pfrom_->GetId());
    }
    return assignmentResult;
}
bool AcceptBlockValidator::connectActiveChain(const CBlock& block, CValidationState& state) const
{
    if (!chainExtensionService_.updateActiveChain(state, &block))
        return error("%s : updateActiveChain failed", __func__);

    return true;
}
bool AcceptBlockValidator::checkBlockRequirements(const CBlock& block, CValidationState& state) const
{
    const bool checked = CheckBlock(block, state);
    if (!CheckBlockSignature(block))
        return error("%s : bad proof-of-stake block signature",__func__);

    const auto& blockMap = chainstate_.GetBlockMap();

    if (block.GetHash() != chainParameters_.HashGenesisBlock() && pfrom_ != NULL) {
        //if we get this far, check if the prev block is our prev block, if not then request sync and return false
        const auto mi = blockMap.find(block.hashPrevBlock);
        if (mi == blockMap.end()) {
            pfrom_->PushMessage("getblocks", chainstate_.ActiveChain().GetLocator(), uint256(0));
            return false;
        }
    }
    if(!checked)
    {
        LOCK(mainCriticalSection_);
        MarkBlockAsReceived(block.GetHash());
        return error("%s : CheckBlock FAILED for block %s", __func__, block.GetHash());
    }
    return true;
}