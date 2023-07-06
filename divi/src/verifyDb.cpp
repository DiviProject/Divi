// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Copyright (c) 2017-2020 The DIVI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <verifyDb.h>

#include <coins.h>
#include <Logging.h>
#include <chain.h>
#include <primitives/block.h>

#include <BlockUndo.h>
#include <ValidationState.h>
#include <BlockDiskDataReader.h>
#include <ui_interface.h>
#include <boost/thread.hpp>
#include <BlockConnectionService.h>
#include <ChainstateManager.h>
#include <spork.h>
#include <BlockCheckingHelpers.h>


CVerifyDB::CVerifyDB(
    const CChainParams& chainParameters,
    const I_SuperblockSubsidyContainer& blockSubsidies,
    const I_BlockIncentivesPopulator& incentives,
    ChainstateManager& chainstate,
    const CCoinsView& coinView,
    const CSporkManager& sporkManager,
    CClientUIInterface& clientInterface,
    const unsigned& coinsCacheSize,
    ShutdownListener shutdownListener
    ): blockDiskReader_(new BlockDiskDataReader())
    , coinView_(coinView)
    , chainstate_(chainstate)
    , coinsViewCache_(new CCoinsViewCache(&coinView_))
    , sporkManager_(sporkManager)
    , blockConnectionService_(
        new BlockConnectionService(
            chainParameters,
            blockSubsidies,
            incentives,
            chainstate.GetBlockMap(),
            &chainstate.BlockTree(),
            coinsViewCache_.get(),
            sporkManager_,
            *blockDiskReader_,
            true))
    , activeChain_(chainstate.ActiveChain())
    , clientInterface_(clientInterface)
    , coinsCacheSize_(coinsCacheSize)
    , shutdownListener_(shutdownListener)
{
    clientInterface_.ShowProgress(translate("Verifying blocks..."), 0);
}

CVerifyDB::~CVerifyDB()
{
    clientInterface_.ShowProgress("", 100);
}

bool CVerifyDB::VerifyDB(int nCheckLevel, int nCheckDepth) const
{
    if (activeChain_.Tip() == NULL || activeChain_.Tip()->pprev == NULL)
        return true;

    const unsigned coinsTipCacheSize = chainstate_.CoinsTip().GetCacheSize();
    // Verify blocks in the best chain
    if (nCheckDepth <= 0)
        nCheckDepth = 1000000000; // suffices until the year 19000
    if (nCheckDepth > activeChain_.Height())
        nCheckDepth = activeChain_.Height();
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);

    const CBlockIndex* pindexState = activeChain_.Tip();
    int nGoodTransactions = 0;
    CValidationState state;
    for (const CBlockIndex* pindex = activeChain_.Tip(); pindex && pindex->pprev; pindex = pindex->pprev) {
        boost::this_thread::interruption_point();
        const double fractionOfBlocksChecked = (double)(activeChain_.Height() - pindex->nHeight) / (double)nCheckDepth;
        const int fractionAsProgressPercentage = static_cast<int>(fractionOfBlocksChecked * (nCheckLevel >= 4 ? 50 : 100));
        const int progressValue = std::max(1, std::min(99, fractionAsProgressPercentage));
        clientInterface_.ShowProgress(translate("Verifying blocks..."), progressValue);
        if (pindex->nHeight < activeChain_.Height() - nCheckDepth)
            break;
        CBlock block;
        // check level 0: read from disk
        if (!blockDiskReader_->ReadBlock(pindex,block))
            return error("VerifyDB() : *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash());
        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(block, state))
            return error("VerifyDB() : *** found bad block at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash());
        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex) {
            CBlockUndo undo;
            CDiskBlockPos pos = pindex->GetUndoPos();
            if (!pos.IsNull()) {
                if (!undo.ReadFromDisk(pos, pindex->pprev->GetBlockHash()))
                    return error("VerifyDB() : *** found bad undo data at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash());
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        const unsigned int coinCacheSize = coinsViewCache_->GetCacheSize();
        if (nCheckLevel >= 3 &&
            pindex == pindexState &&
            (coinCacheSize + coinsTipCacheSize) <= coinsCacheSize_)
        {
            if (!blockConnectionService_->DisconnectBlock(state, pindex, true).second)
                return error("VerifyDB() : *** inconsistency in block data at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash());
            pindexState = pindex->pprev;
            nGoodTransactions += block.vtx.size();
        }
        if (shutdownListener_())
            return true;
    }

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4) {
        const CBlockIndex* pindex = pindexState;
        while (pindex != activeChain_.Tip()) {
            boost::this_thread::interruption_point();

            const double fractionOfBlocksPendingReconnection = (double)(activeChain_.Height() - pindex->nHeight) / (double)nCheckDepth;
            const int fractionAsProgressPercentage = 100 - static_cast<int>(fractionOfBlocksPendingReconnection * 50 );
            const int progressValue = std::max(1, std::min(99, fractionAsProgressPercentage));
            clientInterface_.ShowProgress(translate("Verifying blocks..."), progressValue);

            pindex = activeChain_.Next(pindex);
            CBlock block;
            if (!blockDiskReader_->ReadBlock(pindex,block))
                return error("VerifyDB() : *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash());

            /* ConnectBlock may modify some fields in pindex as the block's
               status is updated.  In particular:

                  nUndoPos, nStatus, nMint and nMoneySupply

               In the current situation, we do not want to have the CBlockIndex
               that is already part of the active chain modified.  Thus we
               apply ConnectBlock to a temporary copy, and verify later on
               that the fields computed match the ones we have already.  */
            CBlockIndex indexCopy(*pindex);
            if (!blockConnectionService_->ConnectBlock(block, state, &indexCopy, true))
                return error("VerifyDB() : *** found unconnectable block at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash());
            if (indexCopy.nUndoPos != pindex->nUndoPos
                  || indexCopy.nStatus != pindex->nStatus
                  || indexCopy.nMint != pindex->nMint
                  || indexCopy.nMoneySupply != pindex->nMoneySupply)
                return error("%s: *** attached block index differs from stored data at %d, hash=%s",
                             __func__, pindex->nHeight, pindex->GetBlockHash());
        }
    }

    LogPrintf("No coin database inconsistencies in last %i blocks (%i transactions)\n", activeChain_.Height() - pindexState->nHeight, nGoodTransactions);

    return true;
}
