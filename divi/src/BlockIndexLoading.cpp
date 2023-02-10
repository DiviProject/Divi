#include <BlockIndexLoading.h>

#include <vector>
#include <utility>

#include <primitives/block.h>
#include <BlockCheckingHelpers.h>
#include <BlockDiskDataReader.h>
#include <BlockFileHelpers.h>
#include <BlockFileOpener.h>
#include <BlockInvalidationHelpers.h>
#include <blockmap.h>
#include <BlockUndo.h>
#include <boost/thread.hpp>
#include <chain.h>
#include <ChainstateManager.h>
#include <ChainSyncHelpers.h>
#include <coins.h>
#include <Settings.h>
#include <streams.h>
#include <TransactionLocationReference.h>
#include <txdb.h>
#include <utilstrencodings.h>
#include <utiltime.h>

//////////////////////////////////////////////////////////////////////////////
//
// CBlockIndex loading from disk
//

static std::vector<std::pair<int, CBlockIndex*> > ComputeHeightSortedBlockIndices(BlockMap& blockIndicesByHash)
{
    std::vector<std::pair<int, CBlockIndex*> > heightSortedBlockIndices;
    heightSortedBlockIndices.reserve(blockIndicesByHash.size());
    for (const auto& item : blockIndicesByHash) {
        CBlockIndex* pindex = item.second;
        heightSortedBlockIndices.push_back(std::make_pair(pindex->nHeight, pindex));
    }
    std::sort(heightSortedBlockIndices.begin(), heightSortedBlockIndices.end());
    return heightSortedBlockIndices;
}

static void InitializeBlockIndexGlobalData(BlockMap& blockIndicesByHash)
{
    const std::vector<std::pair<int, CBlockIndex*> > heightSortedBlockIndices = ComputeHeightSortedBlockIndices(blockIndicesByHash);
    auto& blockIndexCandidates = GetBlockIndexCandidates();
    auto& blockIndexSuccessorsByPrevBlockIndex = GetBlockIndexSuccessorsByPreviousBlockIndex();
    for(const PAIRTYPE(int, CBlockIndex*) & item: heightSortedBlockIndices)
    {
        CBlockIndex* pindex = item.second;
        pindex->nChainWork = (pindex->pprev ? pindex->pprev->nChainWork : 0) + pindex->getBlockProof();
        if (pindex->nStatus & BLOCK_HAVE_DATA) {
            if (pindex->pprev) {
                if (pindex->pprev->nChainTx) {
                    pindex->nChainTx = pindex->pprev->nChainTx + pindex->nTx;
                } else {
                    pindex->nChainTx = 0;
                    blockIndexSuccessorsByPrevBlockIndex.insert(std::make_pair(pindex->pprev, pindex));
                }
            } else {
                pindex->nChainTx = pindex->nTx;
            }
        }
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && (pindex->nChainTx || pindex->pprev == NULL))
            blockIndexCandidates.insert(pindex);
        if (pindex->nStatus & BLOCK_FAILED_MASK)
            updateMostWorkInvalidBlockIndex(pindex);
        if (pindex->pprev){
            pindex->BuildSkip();
            CBlockIndex* pAncestor = pindex->GetAncestor(pindex->vLotteryWinnersCoinstakes.height());
            pindex->vLotteryWinnersCoinstakes.updateShallowDataStore(pAncestor->vLotteryWinnersCoinstakes);
        }
        if (pindex->IsValid(BLOCK_VALID_TREE))
            updateBestHeaderBlockIndex(pindex,false);
    }
}
static bool VerifyAllBlockFilesArePresent(const BlockMap& blockIndicesByHash)
{
    LogPrintf("Checking all blk files are present...\n");
    std::set<int> setBlkDataFiles;
    for (const auto& item : blockIndicesByHash) {
        CBlockIndex* pindex = item.second;
        if (pindex->nStatus & BLOCK_HAVE_DATA) {
            setBlkDataFiles.insert(pindex->nFile);
        }
    }
    for (int blockFileNumber: setBlkDataFiles)
    {
        CDiskBlockPos pos(blockFileNumber, 0);
        if (CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull()) {
            return false;
        }
    }
    return true;
}

bool static RollbackCoinDB(
    const CBlockIndex* const finalBlockIndex,
    const CBlockIndex* currentBlockIndex,
    CCoinsViewCache& view)
{
    BlockDiskDataReader blockDataReader;
    while(currentBlockIndex && finalBlockIndex->nHeight < currentBlockIndex->nHeight)
    {
        CBlock block;
        if (!blockDataReader.ReadBlock(currentBlockIndex,block))
            return error("%s: Unable to read block",__func__);

        CBlockUndo blockUndo;
        if(!blockDataReader.ReadBlockUndo(currentBlockIndex,blockUndo))
            return error("%s: failed to read block undo for %s", __func__, block.GetHash());

        for (int transactionIndex = block.vtx.size() - 1; transactionIndex >= 0; transactionIndex--)
        {
            const CTransaction& tx = block.vtx[transactionIndex];
            const TransactionLocationReference txLocationReference(tx, currentBlockIndex->nHeight, transactionIndex);
            const auto* undo = (transactionIndex > 0 ? &blockUndo.vtxundo[transactionIndex - 1] : nullptr);
            const TxReversalStatus status = view.UpdateWithReversedTransaction(tx,txLocationReference,undo);
            if(status != TxReversalStatus::OK)
            {
                return error("%s: unable to reverse transaction\n",__func__);
            }
        }
        view.SetBestBlock(currentBlockIndex->GetBlockHash());
        currentBlockIndex = currentBlockIndex->pprev;
    }
    assert(currentBlockIndex->GetBlockHash() == finalBlockIndex->GetBlockHash());
    return true;
}

bool static RollforwardkCoinDB(
    const CBlockIndex* const finalBlockIndex,
    const CBlockIndex* currentBlockIndex,
    CCoinsViewCache& view)
{
    std::vector<const CBlockIndex*> blocksToRollForward;
    int numberOfBlocksToRollforward = finalBlockIndex->nHeight - currentBlockIndex->nHeight;
    assert(numberOfBlocksToRollforward>=0);
    if(numberOfBlocksToRollforward < 1) return true;

    blocksToRollForward.resize(numberOfBlocksToRollforward);
    for(const CBlockIndex* rollbackBlock = finalBlockIndex;
        rollbackBlock && currentBlockIndex != rollbackBlock;
        rollbackBlock = rollbackBlock->pprev)
    {
        blocksToRollForward[--numberOfBlocksToRollforward] = rollbackBlock;
    }

    BlockDiskDataReader blockDataReader;
    for(const CBlockIndex* nextBlockIndex: blocksToRollForward)
    {
        CBlock block;
        if (!blockDataReader.ReadBlock(nextBlockIndex,block))
            return error("%s: Unable to read block",__func__);

        for(const CTransaction& tx: block.vtx)
        {
            if(!view.HaveInputs(tx)) return error("%s: unable to apply transction\n",__func__);
            CTxUndo undoDummy;
            view.UpdateWithConfirmedTransaction(tx, nextBlockIndex->nHeight, undoDummy);
        }
        view.SetBestBlock(nextBlockIndex->GetBlockHash());
    }
    assert(view.GetBestBlock() == finalBlockIndex->GetBlockHash());
    return true;
}

bool static ResolveConflictsBetweenCoinDBAndBlockDB(
    const BlockMap& blockMap,
    const uint256& bestBlockHashInBlockDB,
    CCoinsViewCache& coinsTip,
    std::string& strError)
{
    if (coinsTip.GetBestBlock() != bestBlockHashInBlockDB)
    {
        const auto mit = blockMap.find(coinsTip.GetBestBlock());
        if (mit == blockMap.end())
        {
            strError = "Coin database corruption detected! Coin database best block is unknown";
            return false;
        }
        const auto iteratorToBestBlock = blockMap.find(bestBlockHashInBlockDB);
        const CBlockIndex* coinDBBestBlockIndex = mit->second;
        const CBlockIndex* blockDBBestBlockIndex = iteratorToBestBlock->second;
        const CBlockIndex* const lastCommonSyncedBlockIndex = LastCommonAncestor(coinDBBestBlockIndex,blockDBBestBlockIndex);

        const int coinsHeight = coinDBBestBlockIndex->nHeight;
        const int blockIndexHeight = blockDBBestBlockIndex->nHeight;
        LogPrintf("%s : pcoinstip synced to block height %d, block index height %d, last common synced height: %d\n",
             __func__, coinsHeight, blockIndexHeight, lastCommonSyncedBlockIndex->nHeight);

        CCoinsViewCache view(&coinsTip);
        if(!RollbackCoinDB(lastCommonSyncedBlockIndex,coinDBBestBlockIndex,view))
        {
            return error("%s: unable to roll back coin db\n",__func__);
        }
        if(!RollforwardkCoinDB(blockDBBestBlockIndex,lastCommonSyncedBlockIndex,view))
        {
            return error("%s: unable to roll forward coin db\n",__func__);
        }
        // Save the updates to disk
        if(!view.Flush())
            return error("%s: unable to flush coin db ammendments to coinsTip\n",__func__);
        if (!coinsTip.Flush())
            LogPrintf("%s : unable to flush coinTip to disk\n", __func__);

        //get the index associated with the point in the chain that pcoinsTip is synced to
        LogPrintf("%s : pcoinstip=%d %s\n", __func__, coinsHeight, coinsTip.GetBestBlock());
    }
    return true;
}

bool static AttemptBlockDBRecovery(
    const BlockMap& blockMap,
    const CCoinsViewCache& coinsTip,
    CBlockTreeDB& blockTree,
    uint256& expectedBestBlockHash,
    Settings& settings)
{
    expectedBestBlockHash = coinsTip.GetBestBlock();
    if(blockMap.find(expectedBestBlockHash)==blockMap.end()) return false;
    settings.SetParameter("-addressindex", "0");
    settings.SetParameter("-spentindex", "0");
    blockTree.WriteIndexingFlags(
        settings.GetBoolArg("-addressindex", false),
        settings.GetBoolArg("-spentindex", false),
        settings.GetBoolArg("-txindex", true));
    return blockTree.WriteBestBlockHash(expectedBestBlockHash);
}

bool static LoadBlockIndexState(Settings& settings, std::string& strError)
{
    ChainstateManager::Reference chainstate;
    auto& chain = chainstate->ActiveChain();
    auto& coinsTip = chainstate->CoinsTip();
    auto& blockMap = chainstate->GetBlockMap();
    auto& blockTree = chainstate->BlockTree();

    if (!blockTree.LoadBlockIndices(blockMap))
        return error("Failed to load block indices from database");

    boost::this_thread::interruption_point();

    // Check presence of blk files
    if(!VerifyAllBlockFilesArePresent(blockMap))
    {
         return error("Some block files that were expected to be found are missing!");
    }

    // Calculate nChainWork
    InitializeBlockIndexGlobalData(blockMap);

    // Load block file info
    BlockFileHelpers::ReadBlockFiles(blockTree);

    //Check if the shutdown procedure was followed on last client exit
    if(settings.ParameterIsSet("-safe_shutdown"))
    {
        blockTree.WriteFlag("shutdown", settings.GetBoolArg("-safe_shutdown",true));
    }
    bool fLastShutdownWasPrepared = true;
    blockTree.ReadFlag("shutdown", fLastShutdownWasPrepared);
    LogPrintf("%s: Last shutdown was prepared: %s\n", __func__, fLastShutdownWasPrepared);

    //Check for inconsistency with block file info and internal state
    if (!fLastShutdownWasPrepared && !settings.GetBoolArg("-forcestart", false) && !settings.reindexingWasRequested())
    {
        uint256 expectedBestBlockHash;
        if(!blockTree.ReadBestBlockHash(expectedBestBlockHash) || blockMap.find(expectedBestBlockHash) == blockMap.end())
        {
            if(!settings.GetBoolArg("-recoverblockdb",false) ||
                !AttemptBlockDBRecovery(blockMap,coinsTip, blockTree, expectedBestBlockHash, settings))
            {
                strError = "Block database corruption detected! Failed to find best block in block index";
                return false;
            }
        }
        if(!ResolveConflictsBetweenCoinDBAndBlockDB(blockMap,expectedBestBlockHash,coinsTip,strError))
        {
            return false;
        }
        if(settings.ParameterIsSet("-safe_shutdown"))
            assert(coinsTip.GetBestBlock() == expectedBestBlockHash && "Coin database and block database have inconsistent best block");
    }

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    blockTree.ReadReindexing(fReindexing);
    settings.setReindexingFlag( settings.isReindexingBlocks() || fReindexing);

    // Check whether we have address, spent or tx indexing enabled
    blockTree.LoadIndexingFlags();

    // If this is written true before the next client init, then we know the shutdown process failed
    blockTree.WriteFlag("shutdown", false);

    // Load pointer to end of best chain
    const auto it = blockMap.find(coinsTip.GetBestBlock());
    if (it == blockMap.end())
        return true;
    chain.SetTip(it->second);

    PruneBlockIndexCandidates(chain);

    LogPrintf("%s: hashBestChain=%s height=%d date=%s\n",
            __func__,
            chain.Tip()->GetBlockHash(), chain.Height(),
            DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chain.Tip()->GetBlockTime()));

    return true;
}

void UnloadBlockIndex(ChainstateManager* chainstate)
{
    GetBlockIndexCandidates().clear();
    updateMostWorkInvalidBlockIndex(nullptr);

    if (chainstate != nullptr)
    {
        auto& blockMap = chainstate->GetBlockMap();

        for(auto& blockHashAndBlockIndex: blockMap)
        {
            delete blockHashAndBlockIndex.second;
        }
        blockMap.clear();
        chainstate->ActiveChain().SetTip(nullptr);
    }
}

bool LoadBlockIndex(Settings& settings, std::string& strError)
{
    // Load block index from databases
    if (!settings.isReindexingBlocks() && !LoadBlockIndexState(settings, strError))
        return false;
    return true;
}
