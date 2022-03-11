#ifndef ACTIVE_CHAIN_MANAGER_H
#define ACTIVE_CHAIN_MANAGER_H
#include <primitives/block.h>
#include <utility>

class CBlock;
class CValidationState;
class CBlockIndex;
class CCoinsViewCache;
class CBlockTreeDB;
struct IndexDatabaseUpdates;
class uint256;
class CTxUndo;
class CBlockUndo;
class CTransaction;
struct TransactionLocationReference;
class I_BlockDataReader;

class ActiveChainManager
{
private:
    const bool& addressIndexingIsEnabled_;
    const bool& spentIndexingIsEnabled_;
    CBlockTreeDB* blocktree_;
    const I_BlockDataReader& blockDataReader_;

    bool ApplyDisconnectionUpdateIndexToDBs(
        const uint256& bestBlockHash,
        IndexDatabaseUpdates& indexDBUpdates,
        CValidationState& state) const;
public:
    ActiveChainManager(
        const bool& addressIndexingIsEnabled,
        const bool& spentIndexingIsEnabled,
        CBlockTreeDB* blocktree,
        const I_BlockDataReader& blockDataReader);
    bool DisconnectBlock(
        const CBlock& block,
        CValidationState& state,
        const CBlockIndex* pindex,
        CCoinsViewCache& coins,
        bool fJustCheck) const;
    /** Disconnects a block given by pindex, which is also first loaded from
     *  disk and returned as part of disconnectedBlockAndStatus.
     *  This method always fully disconnects (i.e. fJustCheck=false).  */
    void DisconnectBlock(
        std::pair<CBlock,bool>& disconnectedBlockAndStatus,
        CValidationState& state,
        const CBlockIndex* pindex,
        CCoinsViewCache& coins,
        const bool updateCoinsCacheOnly = false) const;
};
#endif// ACTIVE_CHAIN_MANAGER_H
