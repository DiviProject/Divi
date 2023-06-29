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
class CTransaction;
class CLevelDBWrapper;

enum FlushStateMode {
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};
/** Flush all state, indexes and buffers to disk. */
void FlushStateToDisk(FlushStateMode mode = FlushStateMode::FLUSH_STATE_ALWAYS);

/** Register a wallet to receive updates from core */
void RegisterValidationInterface(NotificationInterface* pwalletIn);
/** Unregister a wallet from core */
void UnregisterValidationInterface(NotificationInterface* pwalletIn);
/** Unregister all wallets from core */
void UnregisterAllValidationInterfaces();

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
bool IsBlockValidChainExtension(CBlock* pblock);
bool ProcessNewBlock(CValidationState& state, CNode* pfrom, CBlock* pblock, CDiskBlockPos* dbp = NULL);

/** Import blocks from an external file */
bool LoadExternalBlockFile(FILE* fileIn, CDiskBlockPos* dbp = NULL);
/** Initialize a new block tree database + block data on disk */
bool InitBlockIndex();
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
// ***TODO*** probably not the right place for these 2
/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */

/** Check whether we are doing an initial block download (synchronizing from disk or network) */
bool IsInitialBlockDownload();
/** Format a string that describes several potential problems detected by the core */
std::string GetWarnings(std::string strFor);
/** Find the best known block, and make it the tip of the block chain */

bool DisconnectBlocksAndReprocess(int blocks);

// ***TODO***
bool ActivateBestChain(CValidationState& state, const CBlock* pblock = nullptr, bool fAlreadyChecked = false);

/** (try to) add transaction to memory pool **/
bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree, bool* pfMissingInputs = nullptr, bool ignoreFees = false);

/**
 * Check transaction inputs, and make sure any
 * pay-to-script-hash transactions are evaluating IsStandard scripts
 *
 * Why bother? To avoid denial-of-service attacks; an attacker
 * can submit a standard HASH... OP_EQUAL transaction,
 * which will get accepted into blocks. The redemption
 * script can be anything; an attacker could use a very
 * expensive-to-check-upon-redemption script like:
 *   DUP CHECKSIG DROP ... repeated 100 times... OP_1
 */

/**
 * Check for standard transaction types
 * @param[in] mapInputs    Map of previous transactions that have outputs we're spending
 * @return True if all inputs (scriptSigs) use only standard transaction forms
 */
bool AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs);

/** Context-independent validity checks */
bool CheckTransaction(const CTransaction& tx, CValidationState& state);


/** Check for standard transaction types
 * @return True if all outputs (scriptPubKeys) use only standard transaction forms
 */
bool IsStandardTx(const CTransaction& tx, std::string& reason);

/** Functions for validating blocks and updating the block tree */

/** Reprocess a number of blocks to try and get on the correct chain again **/
bool DisconnectBlocksAndReprocess(int blocks);

/** Apply the effects of this block (with given index) on the UTXO set represented by coins */
bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& coins, bool fJustCheck, bool fAlreadyChecked = false);

/** Context-independent validity checks */
bool CheckBlock(const CBlock& block, CValidationState& state);

/** Context-dependent validity checks */
bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex* pindexPrev);
bool ContextualCheckBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindexPrev);

/** Store block on disk. If dbp is provided, the file is known to already reside on disk */
bool AcceptBlock(CBlock& block, CValidationState& state, CBlockIndex** pindex, CDiskBlockPos* dbp = NULL, bool fAlreadyCheckedBlock = false);
bool AcceptBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex** ppindex = NULL);

/** Find the last common block between the parameter chain and a locator. */
const CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator);

/** Mark a block as invalid. */
bool InvalidateBlock(CValidationState& state, CBlockIndex* pindex, const bool updateCoinDatabaseOnly = false);

/** Remove invalidity status from a block and its descendants. */
bool ReconsiderBlock(CValidationState& state, CBlockIndex* pindex);

#endif // BITCOIN_MAIN_H
