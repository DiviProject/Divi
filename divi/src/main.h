// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MAIN_H
#define BITCOIN_MAIN_H

#if defined(HAVE_CONFIG_H)
#include "config/divi-config.h"
#endif

#include "amount.h"
#include <string>

class CChain;
class CBlock;
struct CBlockLocator;
class CBlockHeader;
class CBlockIndex;
class CBlockTreeDB;
class ChainstateManager;
class CSporkDB;
class CInv;
class CScriptCheck;
class NotificationInterface;
class CValidationState;
class CNode;
struct CNodeSignals;
class CTxMemPool;
class CCoinsViewCache;
class CDiskBlockPos;
class CSporkManager;
class CTransaction;
class CLevelDBWrapper;
class I_ChainExtensionService;

/** Flush all state, indexes and buffers to disk. */
void FlushStateToDisk();

/** Get Current Chain Height with acquired lock **/
int GetHeight();

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
bool ProcessNewBlock(ChainstateManager& chainstate, CValidationState& state, CNode* pfrom, CBlock* pblock, CDiskBlockPos* dbp = NULL);

/** Load the block tree and coins database from disk */
bool LoadBlockIndex(std::string& strError);
/** Unload database information.  If a ChainstateManager is present,
 *  the block map inside (and all other in-memory information) is unloaded.
 *  Otherwise just local data (e.g. validated but not yet attached
 *  CBlockIndex instances) is removed.  */
void UnloadBlockIndex(ChainstateManager* chainstate);
/** Process protocol messages received from a given node */
bool ProcessReceivedMessages(CNode* pfrom);
/**
 * Send queued protocol messages to be sent to a give node.
 *
 * @param[in]   pto             The node which we are sending messages to.
 * @param[in]   fSendTrickle    When true send the trickled data, otherwise trickle the data until true.
 */
bool SendMessages(CNode* pto, bool fSendTrickle);
void RespondToRequestForDataFrom(CNode* pfrom);
#endif // BITCOIN_MAIN_H
