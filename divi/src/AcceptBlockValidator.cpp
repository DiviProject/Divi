#include <AcceptBlockValidator.h>

#include <sync.h>
#include <BlockSigning.h>
#include <primitives/block.h>
#include <Logging.h>
#include <NodeStateRegistry.h>
#include <map>
#include <NodeId.h>
#include <MasternodeModule.h>
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
        std::map<uint256, NodeId>& peerIdByBlockHash,
        ChainstateManager& chainstate,
        const CSporkManager& sporkManager,
        CValidationState& state,
        CNode* pfrom,
        CDiskBlockPos* dbp
        ): chainExtensionService_(chainExtensionService)
        , mainCriticalSection_(mainCriticalSection)
        , chainParameters_(chainParameters)
        , peerIdByBlockHash_(peerIdByBlockHash)
        , chainstate_(chainstate)
        , sporkManager_(sporkManager)
        , state_(state)
        , pfrom_(pfrom)
        , dbp_(dbp)
{
}

std::pair<CBlockIndex*, bool> AcceptBlockValidator::validateAndAssignBlockIndex(CBlock& block, bool& blockChecked) const
{
    CBlockIndex* pindex = nullptr;
    {
        LOCK(mainCriticalSection_);   // Replaces the former TRY_LOCK loop because busy waiting wastes too much resources

        MarkBlockAsReceived (block.GetHash ());
        if (!blockChecked) {
            return std::make_pair(pindex,error ("%s : CheckBlock FAILED for block %s", __func__, block.GetHash()));
        }

        // Store to disk
        bool ret = chainExtensionService_.assignBlockIndex(block, chainstate_, sporkManager_, state_, &pindex, dbp_, blockChecked);
        if (pindex && pfrom_) {
            peerIdByBlockHash_[pindex->GetBlockHash ()] = pfrom_->GetId ();
        }
        return std::make_pair(pindex,ret);
    }
}
bool AcceptBlockValidator::connectActiveChain(CBlockIndex* blockIndex, const CBlock& block, bool& blockChecked) const
{
    if (!chainExtensionService_.updateActiveChain(chainstate_, sporkManager_, state_, &block, blockChecked))
        return error("%s : updateActiveChain failed", __func__);

    VoteForMasternodePayee(blockIndex);
    return true;
}
bool AcceptBlockValidator::checkBlockRequirements(const CBlock& block, bool& checked) const
{
    checked = CheckBlock(block, state_);
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
    return true;
}