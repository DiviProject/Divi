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
        std::map<uint256, NodeId>& peerIdByBlockHash,
        const I_ChainExtensionService& chainExtensionService,
        CCriticalSection& mainCriticalSection,
        const CChainParams& chainParameters,
        ChainstateManager& chainstate
        ): peerIdByBlockHash_(peerIdByBlockHash)
        , chainExtensionService_(chainExtensionService)
        , mainCriticalSection_(mainCriticalSection)
        , chainParameters_(chainParameters)
        , chainstate_(chainstate)
{
}

std::pair<CBlockIndex*, bool> AcceptBlockValidator::validateAndAssignBlockIndex(const NodeAndBlockDiskPosition& nodeAndBlockDisk, CBlock& block, CValidationState& state) const
{
    LOCK(mainCriticalSection_);   // Replaces the former TRY_LOCK loop because busy waiting wastes too much resources
    MarkBlockAsReceived(block.GetHash());
    // Store to disk
    std::pair<CBlockIndex*,bool> assignmentResult = chainExtensionService_.assignBlockIndex(block, state, nodeAndBlockDisk.blockDiskPosition);
    if (assignmentResult.first && nodeAndBlockDisk.dataSource) {
        peerIdByBlockHash_[assignmentResult.first->GetBlockHash()] = nodeAndBlockDisk.dataSource->GetId();
    }
    return assignmentResult;
}
bool AcceptBlockValidator::connectActiveChain(const CBlock& block, CValidationState& state) const
{
    if (!chainExtensionService_.updateActiveChain(state, &block))
        return error("%s : updateActiveChain failed", __func__);

    return true;
}
bool AcceptBlockValidator::checkBlockRequirements(const NodeAndBlockDiskPosition& nodeAndBlockDisk, const CBlock& block, CValidationState& state) const
{
    const bool checked = CheckBlock(block, state);
    if (!CheckBlockSignature(block))
        return error("%s : bad proof-of-stake block signature",__func__);

    const auto& blockMap = chainstate_.GetBlockMap();

    if (block.GetHash() != chainParameters_.HashGenesisBlock() && nodeAndBlockDisk.dataSource != NULL) {
        //if we get this far, check if the prev block is our prev block, if not then request sync and return false
        const auto mi = blockMap.find(block.hashPrevBlock);
        if (mi == blockMap.end()) {
            nodeAndBlockDisk.dataSource->PushMessage("getblocks", chainstate_.ActiveChain().GetLocator(), uint256(0));
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