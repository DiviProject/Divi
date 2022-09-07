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

/** Get Current Chain Height with acquired lock **/
int GetHeight();

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
