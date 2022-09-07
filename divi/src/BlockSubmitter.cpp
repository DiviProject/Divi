#include <BlockSubmitter.h>

#include <sync.h>
#include <ChainstateManager.h>
#include <primitives/block.h>
#include <Logging.h>
#include <utilmoneystr.h>
#include <spork.h>
#include <chain.h>
#include <ValidationState.h>
#include <I_BlockValidator.h>
#include <MasternodeModule.h>
#include <clientversion.h>
#include <utiltime.h>

#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/static_visitor.hpp>

/**
 * Process an incoming block. This only returns after the best known valid
 * block is made active. Note that it does not, however, guarantee that the
 * specific block passed to it has been checked for validity!
 *
 * @param[out]  state   This may be set to an Error state if any error occurred processing it, including during validation/connection/etc of otherwise unrelated blocks during reorganisation; or it may be set to an Invalid state if pblock is itself invalid (but this is not guaranteed even when the block is checked). If you want to *possibly* get feedback on whether pblock is valid, you must also install a NotificationInterface - this will have its BlockChecked method called whenever *any* block completes validation.
 * @param[in]   pfrom   The node which we are receiving the block from; it is added to mapBlockSource and may be penalised if the block is invalid.
 * @param[in]   pblock  The block we want to process.
 * @param[out]  dbp     If pblock is stored to disk (or already there), this will be set to its location.
 * @return True if state.IsValid()
 */
bool ProcessNewBlock(const I_BlockValidator& blockValidator, CValidationState& state, CNode* pfrom, CBlock* pblock, CDiskBlockPos* dbp = nullptr)
{
    // Preliminary checks
    int64_t nStartTime = GetTimeMillis();

    NodeAndBlockDiskPosition nodeAndBlockDiskPosition{pfrom,dbp};
    if(!blockValidator.checkBlockRequirements(nodeAndBlockDiskPosition, *pblock,state)) return false;

    std::pair<CBlockIndex*,bool> assignedBlockIndex = blockValidator.validateAndAssignBlockIndex(nodeAndBlockDiskPosition,*pblock,state);
    if(!assignedBlockIndex.second) return false;
    CBlockIndex* pindex = assignedBlockIndex.first;
    assert(pindex != nullptr);

    if(!blockValidator.connectActiveChain(*pblock,state)) return false;

    VoteForMasternodePayee(pindex);
    LogPrintf("%s : ACCEPTED in %ld milliseconds with size=%d\n", __func__, GetTimeMillis() - nStartTime,
              pblock->GetSerializeSize(SER_DISK, CLIENT_VERSION));

    return true;
}

namespace
{
class BlockProcessingVisitor : public boost::static_visitor<bool>
{
public:
    const I_BlockValidator& blockValidator_;
    CValidationState& state_;
    CBlock& block_;
public:
    BlockProcessingVisitor(
        const I_BlockValidator& blockValidator,
        CValidationState& state,
        CBlock& block
        ): blockValidator_(blockValidator)
        , state_(state)
        , block_(block)
    {
    }
    bool operator()(CNode* node) const { return ProcessNewBlock(blockValidator_, state_, node, &block_); }
    bool operator()(CDiskBlockPos* blockDiskPosition) const { return ProcessNewBlock(blockValidator_, state_, nullptr, &block_, blockDiskPosition); }
};

}

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
    if (!IsBlockValidChainExtension(&block) || !ProcessNewBlock(blockValidator_, state, NULL, &block))
        return error("%s : block not accepted",__func__);

    return true;
}

bool BlockSubmitter::acceptBlockForChainExtension(CValidationState& state, CBlock& block, BlockDataSource blockDataSource) const
{
    return boost::apply_visitor(BlockProcessingVisitor(blockValidator_,state,block), blockDataSource);
}

bool BlockSubmitter::loadBlockForChainExtension(CValidationState& state, CBlock& block, CDiskBlockPos* blockfilePositionData) const
{
    return ProcessNewBlock(blockValidator_, state, NULL, &block, blockfilePositionData);
}