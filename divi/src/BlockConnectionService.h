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

class BlockConnectionService
{
private:
    const bool addressIndexingIsEnabled_;
    const bool spentIndexingIsEnabled_;
    CBlockTreeDB* blocktree_;
    CCoinsViewCache* const coinTip_;
    const I_BlockDataReader& blockDataReader_;
    const bool modifyCoinCacheInplace_;

    bool ApplyDisconnectionUpdateIndexToDBs(
        const uint256& bestBlockHash,
        IndexDatabaseUpdates& indexDBUpdates,
        CValidationState& state) const;

public:
    BlockConnectionService(
        CBlockTreeDB* blocktree,
        CCoinsViewCache* coinTip,
        const I_BlockDataReader& blockDataReader,
        const bool modifyCoinCacheInplace);

    bool DisconnectBlock(
        const CBlock& block,
        CValidationState& state,
        const CBlockIndex* pindex,
        CCoinsViewCache& coins,
        bool fJustCheck) const;
    /** Disconnects a block given by pindex, which is also first loaded from
     *  disk and returned as part of disconnectedBlockAndStatus.
     *  This method always fully disconnects (i.e. fJustCheck=false).  */
    std::pair<CBlock,bool> DisconnectBlock(
        CValidationState& state,
        const CBlockIndex* pindex,
        const bool updateCoinsCacheOnly) const;
};
#endif// ACTIVE_CHAIN_MANAGER_H
