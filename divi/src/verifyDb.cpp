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
#include <BlockDiskAccessor.h>
#include <ui_interface.h>
#include <boost/thread.hpp>
#include <BlockConnectionService.h>
#include <BlockDiskAccessor.h>
#include <ChainstateManager.h>
#include <spork.h>


/** Apply the effects of this block (with given index) on the UTXO set represented by coins */
bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, ChainstateManager& chainstate, const CSporkManager& sporkManager, CCoinsViewCache& coins, bool fJustCheck, bool fAlreadyChecked = false);
bool CheckBlock(const CBlock& block, CValidationState& state);

CVerifyDB::CVerifyDB(
    ChainstateManager& chainstate,
    const CSporkManager& sporkManager,
    CClientUIInterface& clientInterface,
    const unsigned& coinsCacheSize,
    ShutdownListener shutdownListener
    ): blockDiskReader_(new BlockDiskDataReader())
    , chainManager_(new BlockConnectionService(&chainstate.BlockTree(), *blockDiskReader_))
    , chainstate_(chainstate)
    , sporkManager_(sporkManager)
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

bool CVerifyDB::VerifyDB(const CCoinsView* coinsview, unsigned coinsTipCacheSize, int nCheckLevel, int nCheckDepth) const
{
    if (activeChain_.Tip() == NULL || activeChain_.Tip()->pprev == NULL)
        return true;

    // Verify blocks in the best chain
    if (nCheckDepth <= 0)
        nCheckDepth = 1000000000; // suffices until the year 19000
    if (nCheckDepth > activeChain_.Height())
        nCheckDepth = activeChain_.Height();
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview);
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
        if (!ReadBlockFromDisk(block, pindex))
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
        const unsigned int coinCacheSize = coins.GetCacheSize();
        if (nCheckLevel >= 3 &&
            pindex == pindexState &&
            (coinCacheSize + coinsTipCacheSize) <= coinsCacheSize_)
        {
            if (!chainManager_->DisconnectBlock(block, state, pindex, coins, true))
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
            if (!ReadBlockFromDisk(block, pindex))
                return error("VerifyDB() : *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash());

            /* ConnectBlock may modify some fields in pindex as the block's
               status is updated.  In particular:

                  nUndoPos, nStatus, nMint and nMoneySupply

               In the current situation, we do not want to have the CBlockIndex
               that is already part of the active chain modified.  Thus we
               apply ConnectBlock to a temporary copy, and verify later on
               that the fields computed match the ones we have already.  */
            CBlockIndex indexCopy(*pindex);
            if (!ConnectBlock(block, state, &indexCopy, chainstate_, sporkManager_, coins, true))
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
