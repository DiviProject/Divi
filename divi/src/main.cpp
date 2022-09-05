// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"

#include <BlockConnectionService.h>
#include <AcceptBlockValidator.h>
#include "addrman.h"
#include "alert.h"
#include <I_BlockValidator.h>
#include <BlockCheckingHelpers.h>
#include <blockmap.h>
#include "BlockFileOpener.h"
#include "BlockDiskAccessor.h"
#include <BlockDiskDataReader.h>
#include <BlockRejects.h>
#include "BlockRewards.h"
#include "BlockSigning.h"
#include <ChainTipManager.h>
#include <ChainstateManager.h>
#include "chainparams.h"
#include "checkqueue.h"
#include "coins.h"
#include <defaultValues.h>
#include "FeeRate.h"
#include <FlushChainState.h>
#include "init.h"
#include "kernel.h"
#include "masternode-payments.h"
#include "masternodeman.h"
#include "merkleblock.h"
#include "net.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/sigcache.h"
#include "script/standard.h"
#include "spork.h"
#include "sporkdb.h"
#include "sync.h"
#include "tinyformat.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "undo.h"
#include "util.h"
#include "utilmoneystr.h"
#include <UtxoCheckingAndUpdating.h>
#include "NotificationInterface.h"
#include "FeeAndPriorityCalculator.h"
#include <SuperblockSubsidyContainer.h>
#include <BlockIncentivesPopulator.h>
#include <BlockIndexLotteryUpdater.h>
#include <sstream>
#include "Settings.h"
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <BlockUndo.h>
#include <ValidationState.h>
#include <scriptCheck.h>
#include <BlockFileInfo.h>
#include <TransactionOpCounting.h>
#include <OrphanTransactions.h>
#include <MasternodeModule.h>
#include <IndexDatabaseUpdates.h>
#include <BlockTransactionChecker.h>
#include <NodeState.h>
#include <PeerBanningService.h>
#include <utilstrencodings.h>
#include <NodeStateRegistry.h>
#include <Node.h>
#include <TransactionSearchIndexes.h>
#include <ProofOfStakeModule.h>
#include <BlockFileHelpers.h>
#include <ThreadManagementHelpers.h>
#include <MainNotificationRegistration.h>
#include <Warnings.h>
#include <ForkWarningHelpers.h>
#include <BlockInvalidationHelpers.h>
#include <I_ChainTipManager.h>
#include <MostWorkChainTransitionMediator.h>
#include <ChainSyncHelpers.h>
#include <TransactionFinalityHelpers.h>
#include <ChainExtensionService.h>
#include <MempoolConsensus.h>

using namespace boost;
using namespace std;

extern Settings& settings;

#if defined(NDEBUG)
#error "DIVI cannot be compiled without assertions."
#endif

// 6 comes from OPCODE (1) + vch.size() (1) + BIGNUM size (4)
#define SCRIPT_OFFSET 6
// For Script size (BIGNUM/Uint256 size)
#define BIGNUM_SIZE   4
/**
 * Global state
 */

CCriticalSection cs_main;
std::map<uint256, uint256> mapProofOfStake;

//////////////////////////////////////////////////////////////////////////////
//
// dispatching functions
//

//////////////////////////////////////////////////////////////////////////////
//
// Registration of network node signals.
//
int GetHeight()
{
    const ChainstateManager::Reference chainstate;
    while (true) {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) {
            MilliSleep(50);
            continue;
        }
        return chainstate->ActiveChain().Height();
    }
}

const CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator)
{
    const ChainstateManager::Reference chainstate;
    const auto& blockMap = chainstate->GetBlockMap();

    // Find the first block the caller has in the main chain
    for(const uint256& hash: locator.vHave) {
        const auto mi = blockMap.find(hash);
        if (mi != blockMap.end()) {
            const CBlockIndex* pindex = mi->second;
            if (chain.Contains(pindex))
                return pindex;
        }
    }
    return chain.Genesis();
}

//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

bool FindBlockPos(CValidationState& state, CDiskBlockPos& pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown = false)
{
    if(fKnown) return BlockFileHelpers::FindKnownBlockPos(state,pos,nAddSize,nHeight,nTime);
    else return BlockFileHelpers::FindUnknownBlockPos(state,pos,nAddSize,nHeight,nTime);
}
/**
 * Update the on-disk chain state.
 * The caches and indexes are flushed if either they're too large, forceWrite is set, or
 * fast is not set and it's been a while since the last write.
 */


void FlushStateToDisk()
{
    static MainNotificationSignals& notificationSignals = GetMainNotificationInterface();
    CValidationState state;
    FlushStateToDisk(state, FlushStateMode::FLUSH_STATE_ALWAYS, notificationSignals,cs_main);
}


CBlockIndex* AddToBlockIndex(const CBlock& block)
{
    ChainstateManager::Reference chainstate;
    auto& blockMap = chainstate->GetBlockMap();

    const auto& sporkManager = GetSporkManager();
    const BlockIndexLotteryUpdater lotteryUpdater(Params(), chainstate->ActiveChain(), sporkManager);
    // Check for duplicate
    const uint256 hash = block.GetHash();
    const auto it = blockMap.find(hash);
    if (it != blockMap.end())
        return it->second;

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(block);
    assert(pindexNew);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    const auto mi = blockMap.insert(std::make_pair(hash, pindexNew)).first;

    pindexNew->phashBlock = &((*mi).first);
    const auto miPrev = blockMap.find(block.hashPrevBlock);
    if (miPrev != blockMap.end()) {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        pindexNew->BuildSkip();

        //update previous block pointer
        pindexNew->pprev->pnext = pindexNew;

        // ppcoin: compute chain trust score
        pindexNew->bnChainTrust = (pindexNew->pprev ? pindexNew->pprev->bnChainTrust : 0) + pindexNew->GetBlockTrust();

        // ppcoin: compute stake entropy bit for stake modifier
        if (!pindexNew->SetStakeEntropyBit(pindexNew->GetStakeEntropyBit()))
            LogPrintf("%s : SetStakeEntropyBit() failed \n",__func__);

        // ppcoin: record proof-of-stake hash value
        if (pindexNew->IsProofOfStake()) {
            if (!mapProofOfStake.count(hash))
                LogPrintf("%s : hashProofOfStake not found in map \n",__func__);
            pindexNew->hashProofOfStake = mapProofOfStake[hash];
        }

        // ppcoin: compute stake modifier
        SetStakeModifiersForNewBlockIndex(blockMap, pindexNew);
    }
    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);
    updateBestHeaderBlockIndex(pindexNew,true);

    //update previous block pointer
    if (pindexNew->nHeight)
        pindexNew->pprev->pnext = pindexNew;

    lotteryUpdater.UpdateBlockIndexLotteryWinners(block,pindexNew);

    BlockFileHelpers::RecordDirtyBlockIndex(pindexNew);

    return pindexNew;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
bool ReceivedBlockTransactions(const CBlock& block, CBlockIndex* pindexNew, const CDiskBlockPos& pos)
{
    if (block.IsProofOfStake())
        pindexNew->SetProofOfStake();
    pindexNew->nTx = block.vtx.size();
    pindexNew->nChainTx = 0;
    pindexNew->nFile = pos.nFile;
    pindexNew->nDataPos = pos.nPos;
    pindexNew->nUndoPos = 0;
    pindexNew->nStatus |= BLOCK_HAVE_DATA;
    pindexNew->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
    BlockFileHelpers::RecordDirtyBlockIndex(pindexNew);
    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();
    UpdateBlockCandidatesAndSuccessors(chain,pindexNew);
    return true;
}

static int64_t timeOfLastChainTipUpdate =0;
std::unique_ptr<ChainExtensionService> chainExtensionService;
void InitializeChainExtensionService(const MasternodeModule& masternodeModule)
{
    assert(chainExtensionService == nullptr);
    chainExtensionService.reset(
        new ChainExtensionService(
            timeOfLastChainTipUpdate,
            GetTransactionMemoryPool(),
            masternodeModule,
            GetMainNotificationInterface(),
            cs_main,
            settings,
            Params(),
            GetSporkManager(),
            GetBlockIndexSuccessorsByPreviousBlockIndex(),
            GetBlockIndexCandidates()));
}
void FinalizeChainExtensionService()
{
    chainExtensionService.reset();
}
I_ChainExtensionService& GetChainExtensionService()
{
    return *chainExtensionService;
}

bool ProcessNewBlock(ChainstateManager& chainstate, CValidationState& state, CNode* pfrom, CBlock* pblock, CDiskBlockPos* dbp)
{
    // Preliminary checks
    int64_t nStartTime = GetTimeMillis();

    AcceptBlockValidator blockValidator(GetChainExtensionService(), cs_main, Params(), chainstate, pfrom, dbp);
    if(!blockValidator.checkBlockRequirements(*pblock,state)) return false;

    std::pair<CBlockIndex*,bool> assignedBlockIndex = blockValidator.validateAndAssignBlockIndex(*pblock,state);
    if(!assignedBlockIndex.second) return false;
    CBlockIndex* pindex = assignedBlockIndex.first;
    assert(pindex != nullptr);

    if(!blockValidator.connectActiveChain(*pblock,state)) return false;

    VoteForMasternodePayee(pindex);
    LogPrintf("%s : ACCEPTED in %ld milliseconds with size=%d\n", __func__, GetTimeMillis() - nStartTime,
              pblock->GetSerializeSize(SER_DISK, CLIENT_VERSION));

    return true;
}

static std::vector<std::pair<int, CBlockIndex*> > ComputeHeightSortedBlockIndices(BlockMap& blockIndicesByHash)
{
    std::vector<std::pair<int, CBlockIndex*> > heightSortedBlockIndices;
    heightSortedBlockIndices.reserve(blockIndicesByHash.size());
    for (const auto& item : blockIndicesByHash) {
        CBlockIndex* pindex = item.second;
        heightSortedBlockIndices.push_back(make_pair(pindex->nHeight, pindex));
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
        pindex->nChainWork = (pindex->pprev ? pindex->pprev->nChainWork : 0) + GetBlockProof(*pindex);
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
            strError = "The wallet has been not been closed gracefully, causing the transaction database to be out of sync with the block database. Coin db best block unknown";;
            return false;
        }
        const auto iteratorToBestBlock = blockMap.find(bestBlockHashInBlockDB);
        if (iteratorToBestBlock == blockMap.end())
        {
            strError = "The wallet has been not been closed gracefully, causing the transaction database to be out of sync with the block database. Block db best block unknown";
            return false;
        }
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

bool static LoadBlockIndexState(string& strError)
{
    ChainstateManager::Reference chainstate;
    auto& chain = chainstate->ActiveChain();
    auto& coinsTip = chainstate->CoinsTip();
    auto& blockMap = chainstate->GetBlockMap();
    auto& blockTree = chainstate->BlockTree();

    if (!blockTree.LoadBlockIndices(blockMap))
        return error("Failed to load block indices from database");

    boost::this_thread::interruption_point();

    // Calculate nChainWork
    InitializeBlockIndexGlobalData(blockMap);

    // Load block file info
    BlockFileHelpers::ReadBlockFiles(blockTree);

    // Check presence of blk files
    if(!VerifyAllBlockFilesArePresent(blockMap)) return error("Some block files that were expected to be found are missing!");

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
        if(!blockTree.ReadBestBlockHash(expectedBestBlockHash) || !ResolveConflictsBetweenCoinDBAndBlockDB(blockMap,expectedBestBlockHash,coinsTip,strError))
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

bool LoadBlockIndex(string& strError)
{
    // Load block index from databases
    if (!settings.isReindexingBlocks() && !LoadBlockIndexState(strError))
        return false;
    return true;
}

//////////////////////////////////////////////////////////////////////////////
//
// CAlert
//


//////////////////////////////////////////////////////////////////////////////
//
// Messages
//


bool static AlreadyHave(const CTxMemPool& mempool, const CInv& inv)
{
    switch (inv.GetType()) {
    case MSG_TX: {
        bool txInMap = false;
        txInMap = mempool.exists(inv.GetHash());
        return txInMap || OrphanTransactionIsKnown(inv.GetHash());
    }

    case MSG_BLOCK: {
        const ChainstateManager::Reference chainstate;
        return chainstate->GetBlockMap().count(inv.GetHash()) > 0;
    }
    case MSG_TXLOCK_REQUEST:
        return false;
    case MSG_TXLOCK_VOTE:
        return false;
    case MSG_SPORK:
        return SporkDataIsKnown(inv.GetHash());
    case MSG_MASTERNODE_WINNER:
        return MasternodeWinnerIsKnown(inv.GetHash());
    case MSG_MASTERNODE_ANNOUNCE:
        return MasternodeIsKnown(inv.GetHash());
    case MSG_MASTERNODE_PING:
        return MasternodePingIsKnown(inv.GetHash());
    }
    // Don't know what it is, just say we already got one
    return true;
}

static bool PushKnownInventory(CNode* pfrom, const CInv& inv)
{
    bool pushed = false;
    InventoryType type = static_cast<InventoryType>(inv.GetType());
    switch (type)
    {
    case InventoryType::MSG_TX:
        {
            CTransaction tx;
            if (GetTransactionMemoryPool().lookup(inv.GetHash(), tx))
            {
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss.reserve(1000);
                ss << tx;
                pfrom->PushMessage("tx", ss);
                pushed = true;
            }
        }
        break;
    case InventoryType::MSG_SPORK:
        pushed = ShareSporkDataWithPeer(pfrom,inv.GetHash());
        break;
    case InventoryType::MSG_MASTERNODE_WINNER:
        pushed = ShareMasternodeWinnerWithPeer(pfrom,inv.GetHash());
        break;
    case InventoryType::MSG_MASTERNODE_ANNOUNCE:
        pushed = ShareMasternodeBroadcastWithPeer(pfrom,inv.GetHash());
        break;
    case InventoryType::MSG_MASTERNODE_PING:
        pushed = ShareMasternodePingWithPeer(pfrom,inv.GetHash());
        break;
    case InventoryType::MSG_FILTERED_BLOCK:
    case InventoryType::MSG_TXLOCK_REQUEST:
    case InventoryType::MSG_BUDGET_VOTE:
    case InventoryType::MSG_MASTERNODE_SCANNING_ERROR:
    case InventoryType::MSG_BUDGET_PROPOSAL:
    case InventoryType::MSG_BUDGET_FINALIZED:
    case InventoryType::MSG_BUDGET_FINALIZED_VOTE:
    case InventoryType::MSG_MASTERNODE_QUORUM:
    default:
        break;
    }
    return pushed;
}

static std::pair<const CBlockIndex*, bool> GetBlockIndexOfRequestedBlock(NodeId nodeId, const uint256& blockHash)
{
    bool send = false;
    CBlockIndex* pindex = nullptr;
    {
        LOCK(cs_main);

        const ChainstateManager::Reference chainstate;
        const auto& blockMap = chainstate->GetBlockMap();
        const auto& chain = chainstate->ActiveChain();

        const auto mi = blockMap.find(blockHash);
        if (mi != blockMap.end())
        {
            pindex = mi->second;
            if (chain.Contains(mi->second)) {
                send = true;
            } else {
                // To prevent fingerprinting attacks, only send blocks outside of the active
                // chain if they are valid, and no more than a max reorg depth than the best header
                // chain we know about.
                send = mi->second->IsValid(BLOCK_VALID_SCRIPTS) &&
                    GetBestHeaderBlockHeight() > 0 &&
                    (chain.Height() - mi->second->nHeight < Params().MaxReorganizationDepth());
                if (!send) {
                    LogPrintf("%s: ignoring request from peer=%i for old block that isn't in the main chain\n",__func__, nodeId);
                }
            }
        }
    }
    return std::make_pair(pindex,send);
}

static void PushCorrespondingBlockToPeer(CNode* pfrom, const CBlockIndex* blockToPush,bool isBlock)
{
    // Send block from disk
    CBlock block;
    if (!ReadBlockFromDisk(block, blockToPush))
        assert(!"cannot load block from disk");
    if (isBlock)
    {
        pfrom->PushMessage("block", block);
    }
    else // MSG_FILTERED_BLOCK)
    {
        LOCK(pfrom->cs_filter);
        if (pfrom->pfilter) {
            CMerkleBlock merkleBlock(block, *pfrom->pfilter);
            pfrom->PushMessage("merkleblock", merkleBlock);
            // CMerkleBlock just contains hashes, so also push any transactions in the block the client did not see
            // This avoids hurting performance by pointlessly requiring a round-trip
            // Note that there is currently no way for a node to request any single transactions we didnt send here -
            // they must either disconnect and retry or request the full block.
            // Thus, the protocol spec specified allows for us to provide duplicate txn here,
            // however we MUST always provide at least what the remote peer needs
            typedef std::pair<unsigned int, uint256> PairType;
            for(PairType& pair: merkleBlock.vMatchedTxn)
            {
                if (!pfrom->setInventoryKnown.count(CInv(MSG_TX, pair.second)))
                    pfrom->PushMessage("tx", block.vtx[pair.first]);
            }
        }
        // else
        // no response
    }
}

void static ProcessGetData(CNode* pfrom, std::deque<CInv>& requestsForData)
{
    const ChainstateManager::Reference chainstate;

    auto it = requestsForData.begin();

    std::vector<CInv> vNotFound;

    while (
        it != requestsForData.end() &&
        pfrom->GetSendBufferStatus() != NodeBufferStatus::IS_FULL)
    {
        const CInv& inv = *it;
        {
            boost::this_thread::interruption_point();
            it++;

            if (inv.GetType() == MSG_BLOCK || inv.GetType() == MSG_FILTERED_BLOCK)
            {
                std::pair<const CBlockIndex*, bool> blockIndexAndSendStatus = GetBlockIndexOfRequestedBlock(pfrom->GetId(),inv.GetHash());
                // Don't send not-validated blocks
                if (blockIndexAndSendStatus.second &&
                    (blockIndexAndSendStatus.first->nStatus & BLOCK_HAVE_DATA))
                {
                    PushCorrespondingBlockToPeer(pfrom, blockIndexAndSendStatus.first,inv.GetType() == MSG_BLOCK);

                    // Trigger them to send a getblocks request for the next batch of inventory
                    if (inv.GetHash() == pfrom->hashContinue) {
                        // Bypass PushInventory, this must send even if redundant,
                        // and we want it right after the last block so they don't
                        // wait for other stuff first.
                        std::vector<CInv> vInv;
                        vInv.push_back(CInv(MSG_BLOCK, chainstate->ActiveChain().Tip()->GetBlockHash()));
                        pfrom->PushMessage("inv", vInv);
                        pfrom->hashContinue = 0;
                    }
                }
            }
            else if (inv.IsKnownType())
            {
                // Send stream from relay memory
                if(!RepeatRelayedInventory(pfrom,inv) && !PushKnownInventory(pfrom,inv))
                {
                    vNotFound.push_back(inv);
                }
            }

            if (inv.GetType() == MSG_BLOCK || inv.GetType() == MSG_FILTERED_BLOCK)
                break;
        }
    }

    requestsForData.erase(requestsForData.begin(), it);

    if (!vNotFound.empty()) {
        // Let the peer know that we didn't find what it asked for, so it doesn't
        // have to wait around forever. Currently only SPV clients actually care
        // about this message: it's needed when they are recursively walking the
        // dependencies of relevant unconfirmed transactions. SPV clients want to
        // do that because they want to know about (and store and rebroadcast and
        // risk analyze) the dependencies of transactions relevant to them, without
        // having to download the entire memory pool.
        pfrom->PushMessage("notfound", vNotFound);
    }
}

void RespondToRequestForDataFrom(CNode* pfrom)
{
    ProcessGetData(pfrom, pfrom->GetRequestForDataQueue());
}

constexpr const char* NetworkMessageType_VERSION = "version";
static bool SetPeerVersionAndServices(CNode* pfrom, CAddrMan& addrman, CDataStream& vRecv)
{
    // Each connection can only send one version message
    static const std::string lastCommand = std::string(NetworkMessageType_VERSION);
    if (pfrom->GetVersion() != 0) {
        pfrom->PushMessage("reject", lastCommand, REJECT_DUPLICATE, string("Duplicate version message"));
        Misbehaving(pfrom->GetNodeState(), 1,"Duplicated version message");
        return false;
    }

    int64_t nTime;
    CAddress addrMe;
    CAddress addrFrom;
    uint64_t nNonce = 1;

    int nodeVersion;
    uint64_t bitmaskOfNodeServices;
    vRecv >> nodeVersion >> bitmaskOfNodeServices >> nTime >> addrMe;
    pfrom->SetVersionAndServices(nodeVersion,bitmaskOfNodeServices);
    if (pfrom->DisconnectOldProtocol(ActiveProtocol(), lastCommand))
    {
        PeerBanningService::Ban(GetTime(),pfrom->GetCAddress());
        return false;
    }

    if (!vRecv.empty())
        vRecv >> addrFrom >> nNonce;
    if (!vRecv.empty()) {
        vRecv >> LIMITED_STRING(pfrom->strSubVer, 256);
        pfrom->cleanSubVer = SanitizeString(pfrom->strSubVer);
    }
    if (!vRecv.empty())
        vRecv >> pfrom->nStartingHeight;
    if (!vRecv.empty())
        vRecv >> pfrom->fRelayTxes; // set to true after we get the first filter* message
    else
        pfrom->fRelayTxes = true;

    // Disconnect if we connected to ourself
    if (pfrom->IsSelfConnection(nNonce))
    {
        LogPrintf("connected to self at %s, disconnecting\n", pfrom->GetCAddress());
        pfrom->FlagForDisconnection();
        return true;
    }

    pfrom->addrLocal = addrMe;
    if (pfrom->fInbound && addrMe.IsRoutable()) {
        SeenLocal(addrMe);
    }

    // Be shy and don't send version until we hear
    if (pfrom->fInbound)
        pfrom->PushVersion(GetHeight());

    pfrom->fClient = !(pfrom->GetServices() & NODE_NETWORK);

    // Potentially mark this peer as a preferred download peer.
    pfrom->UpdatePreferredDownloadStatus();

    // Change version
    pfrom->PushMessage("verack");

    if(pfrom->fInbound) {
        pfrom->PushMessage("sporkcount", GetSporkManager().GetActiveSporkCount());
    }

    pfrom->SetOutboundSerializationVersion(min(pfrom->GetVersion(), PROTOCOL_VERSION));

    if (!pfrom->fInbound) {
        // Advertise our address
        if (IsListening() && !IsInitialBlockDownload(cs_main,settings)) {
            CAddress addr = GetLocalAddress(&pfrom->GetCAddress());
            if (addr.IsRoutable()) {
                LogPrintf("%s: advertizing address %s\n",__func__, addr);
                pfrom->PushAddress(addr);
            } else if (PeersLocalAddressIsGood(pfrom)) {
                addr.SetIP(pfrom->addrLocal);
                LogPrintf("%s: advertizing address %s\n",__func__, addr);
                pfrom->PushAddress(addr);
            }
        }

        // Get recent addresses
        if (pfrom->fOneShot || pfrom->GetVersion() >= CADDR_TIME_VERSION || addrman.size() < 1000) {
            pfrom->PushMessage("getaddr");
            pfrom->fGetAddr = true;
        }
        addrman.Good(pfrom->GetCAddress());
    } else {
        if (((CNetAddr)pfrom->GetCAddress()) == (CNetAddr)addrFrom) {
            addrman.Add(addrFrom, addrFrom);
            addrman.Good(addrFrom);
        }
    }

    // Relay alerts
    RelayAllAlertsTo(pfrom);

    pfrom->RecordSuccessfullConnection();

    string remoteAddr;
    if (ShouldLogPeerIPs())
        remoteAddr = ", peeraddr=" + pfrom->GetCAddress().ToString();

    LogPrintf("receive version message: %s: version %d, blocks=%d, us=%s, peer=%d%s\n",
                pfrom->cleanSubVer, pfrom->GetVersion(),
                pfrom->nStartingHeight, addrMe, pfrom->id,
                remoteAddr);

    AddTimeData(pfrom->GetCAddress(), nTime);
    return true;
}

bool static ProcessMessage(CNode* pfrom, std::string strCommand, CDataStream& vRecv, int64_t nTimeReceived)
{
    static CAddrMan& addrman = GetNetworkAddressManager();
    LogPrint("net","received: %s (%u bytes) peer=%d\n", SanitizeString(strCommand), vRecv.size(), pfrom->id);
    if (settings.ParameterIsSet("-dropmessagestest") && GetRand(atoi(settings.GetParameter("-dropmessagestest"))) == 0) {
        LogPrint("net","dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    ChainstateManager::Reference chainstate;
    const auto& coinsTip = chainstate->CoinsTip();
    const auto& blockMap = chainstate->GetBlockMap();
    const auto& chain = chainstate->ActiveChain();
    auto& sporkManager = GetSporkManager ();

    if (strCommand == std::string(NetworkMessageType_VERSION))
    {
        if(!SetPeerVersionAndServices(pfrom,addrman,vRecv))
        {
            return false;
        }
        return true;
    }
    else if (pfrom->GetVersion() == 0)
    {
        // Must have a version message before anything else
        Misbehaving(pfrom->GetNodeState(), 1,"Version message has not arrived before other handshake steps");
        return false;
    }
    else if (strCommand == "verack")
    {
        pfrom->SetInboundSerializationVersion(min(pfrom->GetVersion(), PROTOCOL_VERSION));

        // Mark this node as currently connected, so we update its timestamp later.
        if (!pfrom->fInbound) {
            LOCK(cs_main);
            pfrom->SetToCurrentlyConnected();
        }
        return true;
    }

    if (strCommand == "ping")
    {
        pfrom->ProcessReceivedPing(vRecv);
        return true;
    }
    else if (strCommand == "pong")
    {
        pfrom->ProcessReceivedPong(vRecv,nTimeReceived);
        return true;
    }

    if (strCommand == "addr")
    {
        std::vector<CAddress> vAddr;
        vRecv >> vAddr;

        // Don't want addr from older versions unless seeding
        if (pfrom->GetVersion() < CADDR_TIME_VERSION && addrman.size() > 1000)
            return true;
        if (vAddr.size() > 1000) {
            Misbehaving(pfrom->GetNodeState(), 20,"Requested too many addresses");
            return error("message addr size() = %u", vAddr.size());
        }

        // Store the new addresses
        std::vector<CAddress> vAddrOk;
        int64_t nNow = GetAdjustedTime();
        int64_t nSince = nNow - 10 * 60;
        for(CAddress& addr: vAddr) {
            boost::this_thread::interruption_point();

            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable()) {
                // Relay to a limited number of other nodes
                DeterministicallyRelayAddressToLimitedPeers(addr,fReachable ? 2 : 1);
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
        }
        addrman.Add(vAddrOk, pfrom->GetCAddress(), 2 * 60 * 60);
        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        if (pfrom->fOneShot)
            pfrom->FlagForDisconnection();
    }
    else if (strCommand == "inv")
    {
        std::vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ) {
            Misbehaving(pfrom->GetNodeState(), 20, "Asked for too large an inventory of data items");
            return error("message inv size() = %u", vInv.size());
        }

        std::vector<CInv> vToFetch;
        std::vector<const CInv*> blockInventory;
        blockInventory.reserve(vInv.size());
        const bool isImportingFiles = settings.isImportingFiles();
        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++) {
            const CInv& inv = vInv[nInv];

            boost::this_thread::interruption_point();
            pfrom->AddInventoryKnown(inv);

            bool fAlreadyHave = AlreadyHave(GetTransactionMemoryPool(), inv);
            LogPrint("net", "got inv: %s  %s peer=%d\n", inv, fAlreadyHave ? "have" : "new", pfrom->id);

            if (!fAlreadyHave && !isImportingFiles && !settings.isReindexingBlocks() && inv.GetType() != MSG_BLOCK)
                pfrom->AskFor(inv);


            if (inv.GetType() == MSG_BLOCK) {
                UpdateBlockAvailability(blockMap, pfrom->GetNodeState(), inv.GetHash());
                if (!fAlreadyHave && !isImportingFiles && !settings.isReindexingBlocks()) {
                    // Add this to the list of blocks to request
                    blockInventory.push_back(&inv);
                }
            }

            if (pfrom->GetSendBufferStatus()==NodeBufferStatus::IS_OVERFLOWED) {
                Misbehaving(pfrom->GetNodeState(), 50,"Overflowed message buffer");
                return error("Peer %d has exceeded send buffer size", pfrom->GetId());
            }
        }
        {
            LOCK(cs_main);
            for(const CInv* blockInventoryReference: blockInventory)
            {
                if(!BlockIsInFlight(blockInventoryReference->GetHash()))
                {
                    vToFetch.push_back(*blockInventoryReference);
                    LogPrint("net", "getblocks (%d) %s to peer=%d\n", GetBestHeaderBlockHeight(), blockInventoryReference->GetHash(), pfrom->id);
                }
            }
        }

        if (!vToFetch.empty())
            pfrom->PushMessage("getdata", vToFetch);
    }
    else if (strCommand == "getdata")
    {
        std::vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ) {
            Misbehaving(pfrom->GetNodeState(), 20, "Getdata request too large");
            return error("message getdata size() = %u", vInv.size());
        }

        pfrom->HandleRequestForData(vInv);
    }
    else if (strCommand == "getblocks" || strCommand == "getheaders")
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        /* We build up the inventory while holding cs_main (since we access
           a lot of global state, especially chainActive), but then send it
           to the peer without holding onto the lock anymore.  */
        std::vector<CInv> vInv;

        {
        LOCK(cs_main);

        // Find the last block the caller has in the main chain
        const CBlockIndex* pindex = FindForkInGlobalIndex(chain, locator);

        // Send the rest of the chain
        if (pindex)
            pindex = chain.Next(pindex);
        int nLimit = 500;
        LogPrint("net", "getblocks %d to %s limit %d from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop == uint256(0) ? "end" : hashStop.ToString(), nLimit, pfrom->id);
        for (; pindex; pindex = chain.Next(pindex)) {
            // Make sure the inv messages for the requested chain are sent
            // in any case, even if e.g. we have already announced those
            // blocks in the past.  This ensures that the peer will be able
            // to sync properly and not get stuck.
            vInv.emplace_back(MSG_BLOCK, pindex->GetBlockHash());
            if (--nLimit <= 0) {
                // When this block is requested, we'll send an inv that'll make them
                // getblocks the next batch of inventory.
                LogPrint("net", "  getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash());
                pfrom->hashContinue = pindex->GetBlockHash();
                break;
            }
            if (pindex->GetBlockHash() == hashStop) {
                LogPrint("net", "  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash());
                break;
            }
        }
        }

        if (!vInv.empty())
            pfrom->PushMessage("inv", vInv);
    }
    else if (strCommand == "headers" && Params().HeadersFirstSyncingActive())
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        if (IsInitialBlockDownload(cs_main,settings))
            return true;

        const CBlockIndex* pindex = nullptr;
        if (locator.IsNull()) {
            // If locator is null, return the hashStop block
            const auto mi = blockMap.find(hashStop);
            if (mi == blockMap.end())
                return true;
            pindex = (*mi).second;
        } else {
            // Find the last block the caller has in the main chain
            pindex = FindForkInGlobalIndex(chain, locator);
            if (pindex)
                pindex = chain.Next(pindex);
        }

        // we must use CBlocks, as CBlockHeaders won't include the 0x00 nTx count at the end
        std::vector<CBlock> vHeaders;
        int nLimit = MAX_HEADERS_RESULTS;
        LogPrint("net","getheaders %d to %s from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop, pfrom->id);
        for (; pindex; pindex = chain.Next(pindex)) {
            vHeaders.push_back(pindex->GetBlockHeader());
            if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                break;
        }
        pfrom->PushMessage("headers", vHeaders);
    }
    else if (strCommand == "tx" || strCommand == "dstx")
    {
        std::vector<uint256> vWorkQueue;
        std::vector<uint256> vEraseQueue;
        CTransaction tx;

        //masternode signed transaction
        CTxIn vin;
        std::vector<unsigned char> vchSig;

        if (strCommand == "tx") {
            vRecv >> tx;
        }

        CInv inv(MSG_TX, tx.GetHash());
        pfrom->AddInventoryKnown(inv);

        bool fMissingInputs = false;
        CValidationState state;

        {
        LOCK(cs_main);

        CNode::ClearInventoryItem(inv);

        CTxMemPool& mempool = GetTransactionMemoryPool();
        if ( MempoolConsensus::AcceptToMemoryPool(mempool, state, tx, true, &fMissingInputs))
        {
            mempool.check(&coinsTip, blockMap);
            RelayTransactionToAllPeers(tx);
            vWorkQueue.push_back(inv.GetHash());

            LogPrint("mempool", "%s: peer=%d %s : accepted %s (poolsz %u)\n",
                    __func__,
                     pfrom->id, pfrom->cleanSubVer,
                     tx.ToStringShort(),
                     mempool.mapTx.size());

            // Recursively process any orphan transactions that depended on this one
            std::set<NodeId> setMisbehaving;
            for(unsigned int i = 0; i < vWorkQueue.size(); i++)
            {
                const std::set<uint256>& spendingTransactionIds = GetOrphanSpendingTransactionIds(vWorkQueue[i]);
                for(const uint256 &orphanHash: spendingTransactionIds)
                {
                    NodeId fromPeer;
                    const CTransaction &orphanTx = GetOrphanTransaction(orphanHash,fromPeer);
                    bool fMissingInputs2 = false;
                    // Use a dummy CValidationState so someone can't setup nodes to counter-DoS based on orphan
                    // resolution (that is, feeding people an invalid transaction based on LegitTxX in order to get
                    // anyone relaying LegitTxX banned)
                    CValidationState stateDummy;


                    if(setMisbehaving.count(fromPeer))
                        continue;
                    if(MempoolConsensus::AcceptToMemoryPool(mempool, stateDummy, orphanTx, true, &fMissingInputs2)) {
                        LogPrint("mempool", "   accepted orphan tx %s\n", orphanHash);
                        RelayTransactionToAllPeers(orphanTx);
                        vWorkQueue.push_back(orphanHash);
                        vEraseQueue.push_back(orphanHash);
                    } else if(!fMissingInputs2) {
                        int nDos = 0;
                        if(stateDummy.IsInvalid(nDos) && nDos > 0) {
                            // Punish peer that gave us an invalid orphan tx
                            Misbehaving(fromPeer, nDos, "Invalid orphan transaction required by mempool transaction");
                            setMisbehaving.insert(fromPeer);
                            LogPrint("mempool", "   invalid orphan tx %s\n", orphanHash);
                        }
                        // Has inputs but not accepted to mempool
                        // Probably non-standard or insufficient fee/priority
                        LogPrint("mempool", "   removed orphan tx %s\n", orphanHash);
                        vEraseQueue.push_back(orphanHash);
                    }
                    mempool.check(&coinsTip, blockMap);
                }
            }

            for(uint256 hash: vEraseQueue)EraseOrphanTx(hash);
        }
        else if (fMissingInputs)
        {
            AddOrphanTx(tx, pfrom->GetId());

            // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
            unsigned int nMaxOrphanTx = (unsigned int)std::max((int64_t)0, settings.GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS));
            unsigned int nEvicted = LimitOrphanTxSize(nMaxOrphanTx);
            if (nEvicted > 0)
                LogPrint("mempool", "mapOrphan overflow, removed %u tx\n", nEvicted);
        } else if (pfrom->fWhitelisted) {
            // Always relay transactions received from whitelisted peers, even
            // if they are already in the mempool (allowing the node to function
            // as a gateway for nodes hidden behind it).

            RelayTransactionToAllPeers(tx);
        }
        } // cs_main

        int nDoS = 0;
        if (state.IsInvalid(nDoS)) {
            LogPrint("mempool", "%s from peer=%d %s was not accepted into the memory pool: %s\n", tx.ToStringShort(),
                     pfrom->id, pfrom->cleanSubVer,
                     state.GetRejectReason());
            pfrom->PushMessage("reject", strCommand, state.GetRejectCode(),
                               state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.GetHash());
            if (nDoS > 0)
                Misbehaving(pfrom->GetNodeState(), nDoS, "Transaction from peer rejected by memory pool");
        }
    }
    else if (strCommand == "block" && !settings.isImportingFiles() && !settings.isReindexingBlocks()) // Ignore blocks received while importing
    {
        CBlock block;
        vRecv >> block;
        uint256 hashBlock = block.GetHash();
        CInv inv(MSG_BLOCK, hashBlock);
        LogPrint("net", "received block %s peer=%d\n", inv.GetHash(), pfrom->id);

        //sometimes we will be sent their most recent block and its not the one we want, in that case tell where we are
        if (!blockMap.count(block.hashPrevBlock)) {
            if (find(pfrom->vBlockRequested.begin(), pfrom->vBlockRequested.end(), hashBlock) != pfrom->vBlockRequested.end()) {
                //we already asked for this block, so lets work backwards and ask for the previous block
                pfrom->PushMessage("getblocks", chain.GetLocator(), block.hashPrevBlock);
                pfrom->vBlockRequested.push_back(block.hashPrevBlock);
            } else {
                //ask to sync to this block
                pfrom->PushMessage("getblocks", chain.GetLocator(), hashBlock);
                pfrom->vBlockRequested.push_back(hashBlock);
            }
        } else {
            pfrom->AddInventoryKnown(inv);

            CValidationState state;
            if (!blockMap.count(block.GetHash())) {
                ProcessNewBlock(*chainstate, state, pfrom, &block);
                int nDoS;
                if(state.IsInvalid(nDoS)) {
                    pfrom->PushMessage("reject", strCommand, state.GetRejectCode(),
                                       state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.GetHash());
                    if(nDoS > 0) {
                        TRY_LOCK(cs_main, lockMain);
                        if(lockMain) Misbehaving(pfrom->GetNodeState(), nDoS, "Bad block processed");
                    }
                }
                //disconnect this node if its old protocol version
                if(pfrom->DisconnectOldProtocol(ActiveProtocol(), strCommand))
                {
                    PeerBanningService::Ban(GetTime(),pfrom->GetCAddress());
                }
            } else {
                LogPrint("net", "%s : Already processed block %s, skipping ProcessNewBlock()\n", __func__, block.GetHash());
            }
        }
    }
    // This asymmetric behavior for inbound and outbound connections was introduced
    // to prevent a fingerprinting attack: an attacker can send specific fake addresses
    // to users' AddrMan and later request them by sending getaddr messages.
    // Making users (which are behind NAT and can only make outgoing connections) ignore
    // getaddr message mitigates the attack.
    else if ((strCommand == "getaddr") && (pfrom->fInbound))
    {
        pfrom->vAddrToSend.clear();
        std::vector<CAddress> vAddr = addrman.GetAddr();
        for(const CAddress& addr: vAddr)
                pfrom->PushAddress(addr);
    }
    else if (strCommand == "alert" && AlertsAreEnabled())
    {
        CAlert alert;
        vRecv >> alert;

        uint256 alertHash = alert.GetHash();
        if (pfrom->setKnown.count(alertHash) == 0) {
            if (alert.ProcessAlert(settings)) {
                // Relay
                pfrom->setKnown.insert(alertHash);
                RelayAlertToPeers(alert);
            } else {
                // Small DoS penalty so peers that send us lots of
                // duplicate/expired/invalid-signature/whatever alerts
                // eventually get banned.
                // This isn't a Misbehaving(100) (immediate ban) because the
                // peer might be an older or different implementation with
                // a different signature key, etc.
                Misbehaving(pfrom->GetNodeState(), 10, "Unable to process alert message");
            }
        }
    }
    else if (!BloomFiltersAreEnabled() &&
             (strCommand == "filterload" ||
              strCommand == "filteradd" ||
              strCommand == "filterclear"))
    {
        LogPrintf("bloom message=%s\n", strCommand);
        Misbehaving(pfrom->GetNodeState(), 100, "Sent bloom filter msg but they are disabled");
    }
    else if (strCommand == "filterload")
    {
        CBloomFilter filter;
        vRecv >> filter;

        if (!filter.IsWithinSizeConstraints())
            // There is no excuse for sending a too-large filter
            Misbehaving(pfrom->GetNodeState(), 100,"Sent too large a bloom filter message");
        else {
            LOCK(pfrom->cs_filter);
            delete pfrom->pfilter;
            pfrom->pfilter = new CBloomFilter(filter);
            pfrom->pfilter->UpdateEmptyFull();
        }
        pfrom->fRelayTxes = true;
    }
    else if (strCommand == "filteradd")
    {
        std::vector<unsigned char> vData;
        vRecv >> vData;

        // Nodes must NEVER send a data item > 520 bytes (the max size for a script data object,
        // and thus, the maximum size any matched object can have) in a filteradd message
        if (vData.size() > MAX_SCRIPT_ELEMENT_SIZE) {
            Misbehaving(pfrom->GetNodeState(), 100, "Exceeded maximum size of script element to be added to filter");
        } else {
            LOCK(pfrom->cs_filter);
            if (pfrom->pfilter)
                pfrom->pfilter->insert(vData);
            else
                Misbehaving(pfrom->GetNodeState(), 100, "Attempted to load filter before enabling it");
        }
    }
    else if (strCommand == "filterclear")
    {
        LOCK(pfrom->cs_filter);
        delete pfrom->pfilter;
        pfrom->pfilter = new CBloomFilter();
        pfrom->fRelayTxes = true;
    }
    else if (strCommand == "reject")
    {
        if (settings.debugModeIsEnabled()) {
            try {
                string strMsg;
                unsigned char ccode;
                string strReason;
                vRecv >> LIMITED_STRING(strMsg, CMessageHeader::COMMAND_SIZE) >> ccode >> LIMITED_STRING(strReason, MAX_REJECT_MESSAGE_LENGTH);

                ostringstream ss;
                ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

                if (strMsg == "block" || strMsg == "tx") {
                    uint256 hash;
                    vRecv >> hash;
                    ss << ": hash " << hash.ToString();
                }
                LogPrint("net", "Reject %s\n", SanitizeString(ss.str()));
            } catch (std::ios_base::failure& e) {
                // Avoid feedback loops by preventing reject messages from triggering a new reject message.
                LogPrint("net", "Unparseable reject message received\n");
            }
        }
    } else {
        sporkManager.ProcessSpork(cs_main, pfrom, strCommand, vRecv);
        ProcessMasternodeMessages(pfrom,strCommand,vRecv);
    }

    return true;
}

enum NetworkMessageState
{
    SKIP_MESSAGE,
    STOP_PROCESSING,
    VALID,
};
static NetworkMessageState CheckNetworkMessageHeader(
    NodeId id,
    const CNetMessage& msg,
    bool& fOk)
{
    // Scan for message start
    if (memcmp(msg.hdr.pchMessageStart, Params().MessageStart(), MESSAGE_START_SIZE) != 0) {
        LogPrintf("%s: INVALID MESSAGESTART %s peer=%d\n",__func__, SanitizeString(msg.hdr.GetCommand()), id);
        fOk = false;
        return NetworkMessageState::STOP_PROCESSING;
    }

    // Read header
    const CMessageHeader& hdr = msg.hdr;
    if (!hdr.IsValid()) {
        LogPrintf("%s: ERRORS IN HEADER %s peer=%d\n",__func__, SanitizeString(hdr.GetCommand()), id);
        return NetworkMessageState::SKIP_MESSAGE;
    }

    // Message size
    const unsigned int nMessageSize = hdr.nMessageSize;

    // Checksum
    const CDataStream& vRecv = msg.vRecv;
    uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
    unsigned int nChecksum = 0;
    memcpy(&nChecksum, &hash, sizeof(nChecksum));
    if (nChecksum != hdr.nChecksum) {
        LogPrintf("%s(%s, %u bytes): CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n",
                    __func__,SanitizeString(hdr.GetCommand()), nMessageSize, nChecksum, hdr.nChecksum);
        return NetworkMessageState::SKIP_MESSAGE;
    }
    return NetworkMessageState::VALID;
}

// requires LOCK(cs_vRecvMsg)
bool ProcessReceivedMessages(CNode* pfrom)
{
    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //
    bool fOk = true;
    std::deque<CNetMessage>& receivedMessageQueue = pfrom->GetReceivedMessageQueue();
    std::deque<CNetMessage>::iterator iteratorToCurrentMessageToProcess = receivedMessageQueue.begin();
    std::deque<CNetMessage>::iterator iteratorToNextMessageToProcess = receivedMessageQueue.begin();
    while(
        !pfrom->IsFlaggedForDisconnection() &&
        pfrom->GetSendBufferStatus()!=NodeBufferStatus::IS_FULL && // needed, to allow responding
        iteratorToCurrentMessageToProcess != receivedMessageQueue.end() &&
        iteratorToCurrentMessageToProcess->complete()) // end, if an incomplete message is found
    {
        // get next message
        CNetMessage& msg = *iteratorToCurrentMessageToProcess;
        iteratorToNextMessageToProcess = ++iteratorToCurrentMessageToProcess;

        NetworkMessageState messageStatus = CheckNetworkMessageHeader(pfrom->GetId(), msg, fOk);
        if(messageStatus == NetworkMessageState::STOP_PROCESSING)
        {
            break;
        }
        else if(messageStatus == NetworkMessageState::SKIP_MESSAGE)
        {
            continue;
        }
        const CMessageHeader& hdr = msg.hdr;
        std::string strCommand = msg.hdr.GetCommand();

        // Process message
        bool fRet = false;
        try {
            fRet = ProcessMessage(pfrom, strCommand, msg.vRecv, msg.nTime);
            boost::this_thread::interruption_point();
        } catch (std::ios_base::failure& e) {
            pfrom->PushMessage("reject", strCommand, REJECT_MALFORMED, string("error parsing message"));
            if (strstr(e.what(), "end of data")) {
                // Allow exceptions from under-length message on vRecv
                LogPrintf("%s(%s, %u bytes): Exception '%s' caught, normally caused by a message being shorter than its stated length\n",__func__, SanitizeString(strCommand), hdr.nMessageSize, e.what());
            } else if (strstr(e.what(), "size too large")) {
                // Allow exceptions from over-long size
                LogPrintf("%s(%s, %u bytes): Exception '%s' caught\n",__func__, SanitizeString(strCommand), hdr.nMessageSize, e.what());
            } else {
                PrintExceptionContinue(&e, __func__);
            }
        } catch (boost::thread_interrupted) {
            throw;
        } catch (std::exception& e) {
            PrintExceptionContinue(&e, __func__);
        } catch (...) {
            PrintExceptionContinue(NULL, __func__);
        }

        if (!fRet)
            LogPrintf("%s(%s, %u bytes) FAILED peer=%d\n",__func__, SanitizeString(strCommand), hdr.nMessageSize, pfrom->id);

        break;
    }

    // In case the connection got shut down, its receive buffer was wiped
    if (!pfrom->IsFlaggedForDisconnection())
        receivedMessageQueue.erase(receivedMessageQueue.begin(), iteratorToNextMessageToProcess);
    else
        receivedMessageQueue.clear();

    return fOk;
}

static void SendAddresses(CNode* pto)
{
    std::vector<CAddress> vAddr;
    vAddr.reserve(pto->vAddrToSend.size());
    for(const CAddress& addr: pto->vAddrToSend) {
        // returns true if wasn't already contained in the set
        if (pto->setAddrKnown.insert(addr).second) {
            vAddr.push_back(addr);
            // receiver rejects addr messages larger than 1000
            if (vAddr.size() >= 1000) {
                pto->PushMessage("addr", vAddr);
                vAddr.clear();
            }
        }
    }
    pto->vAddrToSend.clear();
    if (!vAddr.empty())
        pto->PushMessage("addr", vAddr);
}

static void CheckForBanAndDisconnectIfNotWhitelisted(CNode* pto)
{
    CNodeState* nodeState = pto->GetNodeState();
    if(!nodeState->fShouldBan) return;
    if (pto->fWhitelisted)
    {
        LogPrintf("Warning: not punishing whitelisted peer %s!\n", pto->GetCAddress());
    }
    else
    {
        pto->FlagForDisconnection();
        if (pto->GetCAddress().IsLocal())
        {
            LogPrintf("Warning: not banning local peer %s!\n", pto->GetCAddress());
        }
        else
        {
            PeerBanningService::Ban(GetTime(),pto->GetCAddress());
        }
    }
    nodeState->fShouldBan = false;
}

static void BeginSyncingWithPeer(CNode* pto)
{
    CNodeState* state = pto->GetNodeState();
    if (!state->Syncing() && !pto->fClient && !settings.isReindexingBlocks()) {
        const ChainstateManager::Reference chainstate;
        const auto& chain = chainstate->ActiveChain();

        // Only actively request headers from a single peer, unless we're close to end of initial download.
        if ( !CNodeState::NodeSyncStarted() || GetBestHeaderBlocktime() > GetAdjustedTime() - 6 * 60 * 60) { // NOTE: was "close to today" and 24h in Bitcoin
            state->RecordNodeStartedToSync();
            pto->PushMessage("getblocks", chain.GetLocator(chain.Tip()), uint256(0));
        }
    }
}
static void SendInventoryToPeer(CNode* pto, bool fSendTrickle)
{
    std::vector<CInv> vInv;
    {
        std::vector<CInv> vInvWait;

        LOCK(pto->cs_inventory);
        vInv.reserve(pto->vInventoryToSend.size());
        vInvWait.reserve(pto->vInventoryToSend.size());
        for (const auto& inv : pto->vInventoryToSend) {
            if (pto->setInventoryKnown.count(inv) > 0)
                continue;

            // trickle out tx inv to protect privacy
            if (inv.GetType() == MSG_TX && !fSendTrickle) {
                // 1/4 of tx invs blast to all immediately
                static uint256 hashSalt;
                if (hashSalt == 0)
                    hashSalt = GetRandHash();
                uint256 hashRand = inv.GetHash() ^ hashSalt;
                hashRand = Hash(BEGIN(hashRand), END(hashRand));
                bool fTrickleWait = ((hashRand & 3) != 0);

                if (fTrickleWait) {
                    vInvWait.push_back(inv);
                    continue;
                }
            }

            // returns true if wasn't already contained in the set
            if (pto->setInventoryKnown.insert(inv).second) {
                vInv.push_back(inv);
                if (vInv.size() >= 1000) {
                    pto->PushMessage("inv", vInv);
                    vInv.clear();
                }
            }
        }
        pto->vInventoryToSend = std::move(vInvWait);
    }
    if (!vInv.empty())
        pto->PushMessage("inv", vInv);
}
static void RequestDisconnectionFromNodeIfStalling(int64_t nNow, CNode* pto)
{
    if (!pto->IsFlaggedForDisconnection() && BlockDownloadHasStalled(pto->GetId(),nNow, 1000000 * BLOCK_STALLING_TIMEOUT)  ) {
        // Stalling only triggers when the block download window cannot move. During normal steady state,
        // the download window should be much larger than the to-be-downloaded set of blocks, so disconnection
        // should only happen during initial block download.
        LogPrintf("Peer=%d is stalling block download, disconnecting\n", pto->id);
        pto->FlagForDisconnection();
    }
    // In case there is a block that has been in flight from this peer for (2 + 0.5 * N) times the block interval
    // (with N the number of validated blocks that were in flight at the time it was requested), disconnect due to
    // timeout. We compensate for in-flight blocks to prevent killing off peers due to our own downstream link
    // being saturated. We only count validated in-flight blocks so peers can't advertize nonexisting block hashes
    // to unreasonably increase our timeout.
    if (!pto->IsFlaggedForDisconnection() && BlockDownloadHasTimedOut(pto->GetId(),nNow,Params().TargetSpacing()) ) {
        pto->FlagForDisconnection();
    }
}
static void CollectBlockDataToRequest(int64_t nNow, CNode* pto, std::vector<CInv>& vGetData)
{
    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();
    const auto& blockMap = chainstate->GetBlockMap();

    if (!pto->IsFlaggedForDisconnection() && !pto->fClient && GetNumberOfBlocksInFlight(pto->GetId()) < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
        std::vector<const CBlockIndex*> vToDownload;
        NodeId staller = -1;
        FindNextBlocksToDownload(blockMap, chain, pto->GetNodeState(), MAX_BLOCKS_IN_TRANSIT_PER_PEER - GetNumberOfBlocksInFlight(pto->GetId()), vToDownload, staller);
        for(const auto* pindex: vToDownload) {
            vGetData.push_back(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            MarkBlockAsInFlight(pto->GetId(), pindex->GetBlockHash(), pindex);
            LogPrintf("Requesting block %s (%d) peer=%d\n", pindex->GetBlockHash(),
                        pindex->nHeight, pto->id);
        }
        if (GetNumberOfBlocksInFlight(pto->GetId()) == 0 && staller != -1) {
            RecordWhenStallingBegan(staller,nNow);
        }
    }
}
void CollectNonBlockDataToRequestAndRequestIt(const CTxMemPool& mempool, CNode* pto, int64_t nNow, std::vector<CInv>& vGetData)
{
    while (!pto->IsFlaggedForDisconnection() && !pto->mapAskFor.empty() && (*pto->mapAskFor.begin()).first <= nNow)
    {
        const CInv& inv = (*pto->mapAskFor.begin()).second;
        if (!AlreadyHave(mempool, inv)) {
            LogPrint("net", "Requesting %s peer=%d\n", inv, pto->id);
            vGetData.push_back(inv);
            if (vGetData.size() >= 1000) {
                pto->PushMessage("getdata", vGetData);
                vGetData.clear();
            }
        }
        pto->mapAskFor.erase(pto->mapAskFor.begin());
    }
    if (!vGetData.empty())
        pto->PushMessage("getdata", vGetData);
}

void RebroadcastSomeMempoolTxs(CTxMemPool& mempool)
{
    TRY_LOCK(cs_main,mainLockAcquired);
    constexpr int maxNumberOfRebroadcastableTransactions = 32;
    if(mainLockAcquired)
    {
        TRY_LOCK(mempool.cs,mempoolLockAcquired);
        if(mempoolLockAcquired)
        {
            LogPrintf("Rebroadcasting mempool transactions\n");
            int numberOfTransactionsCollected = 0;
            const  std::map<uint256, CTxMemPoolEntry>& mempoolTxs = mempool.mapTx;
            for(const auto& mempoolEntryByHash: mempoolTxs)
            {
                const CTransaction& tx = mempoolEntryByHash.second.GetTx();
                bool spendsOtherMempoolTransaction = false;
                for(const auto& input: tx.vin)
                {
                    if(mempoolTxs.count(input.prevout.hash)>0)
                    {
                        spendsOtherMempoolTransaction = true;
                        break;
                    }
                }
                if(!spendsOtherMempoolTransaction)
                {
                    RelayTransactionToAllPeers(tx);
                    ++numberOfTransactionsCollected;
                }
                if(numberOfTransactionsCollected >= maxNumberOfRebroadcastableTransactions) break;
            }
        }
    }
}

void PeriodicallyRebroadcastMempoolTxs(CTxMemPool& mempool)
{
    static int64_t timeOfLastBroadcast = 0;
    static int64_t timeOfNextBroadcast = 0;
    if(GetTime() < timeOfNextBroadcast) return;
    bool nextTimeBroadcastNeedsToBeInitialized = (timeOfNextBroadcast == 0);
    timeOfNextBroadcast = GetTime() + GetRand(30*60);
    if(nextTimeBroadcastNeedsToBeInitialized) return;
    if(timeOfLastChainTipUpdate < timeOfLastBroadcast) return;
    timeOfLastBroadcast = GetTime();
    if(timeOfLastChainTipUpdate > 0)
        RebroadcastSomeMempoolTxs(mempool);
}

bool SendMessages(CNode* pto, bool fSendTrickle)
{
    const ChainstateManager::Reference chainstate;

    {
        if (fSendTrickle) {
            SendAddresses(pto);
        }

        CheckForBanAndDisconnectIfNotWhitelisted(pto);

        // Start block sync
        const CNodeState* state = pto->GetNodeState();
        bool fFetch = state->fPreferredDownload || (!CNodeState::HavePreferredDownloadPeers() && !pto->fClient && !pto->fOneShot); // Download if this is a nice peer, or we have no nice peers and this one might do.
        if(fFetch)
        {
            BeginSyncingWithPeer(pto);
        }
        CTxMemPool& mempool = GetTransactionMemoryPool();
        if(!settings.isReindexingBlocks()) PeriodicallyRebroadcastMempoolTxs(mempool);
        SendInventoryToPeer(pto,fSendTrickle);
        int64_t nNow = GetTimeMicros();
        std::vector<CInv> vGetData;
        {
            LOCK(cs_main);
            RequestDisconnectionFromNodeIfStalling(nNow,pto);
            if(fFetch) CollectBlockDataToRequest(nNow,pto,vGetData);
        }
        CollectNonBlockDataToRequestAndRequestIt(mempool, pto,nNow,vGetData);
    }
    return true;
}