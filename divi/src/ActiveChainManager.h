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
    CBlockTreeDB* blocktree_;
    const I_BlockDataReader& blockDataReader_;

    bool ApplyDisconnectionUpdateIndexToDBs(
        IndexDatabaseUpdates& indexDBUpdates,
        CValidationState& state) const;
    void CollectIndexUpdatesFromInputs(
        CCoinsViewCache& view,
        const CTransaction& tx,
        const TransactionLocationReference& txLocationReference,
        const CTxUndo& txundo,
        IndexDatabaseUpdates& indexDBUpdates) const;
    void CollectIndexUpdatesFromOutputs(
        const CTransaction& tx,
        const TransactionLocationReference& txLocationReference,
        IndexDatabaseUpdates& indexDBUpdates) const;
    bool UpdateDBIndicesFromDisconnection(
        CBlock& block,
        CBlockUndo& blockUndo,
        CValidationState& state,
        CBlockIndex* pindex,
        CCoinsViewCache& view) const;
public:
    ActiveChainManager(
        const bool& addressIndexingIsEnabled,
        CBlockTreeDB* blocktree,
        const I_BlockDataReader& blockDataReader);
    bool DisconnectBlock(
        CBlock& block,
        CValidationState& state,
        CBlockIndex* pindex,
        CCoinsViewCache& coins,
        bool* pfClean = nullptr) const;
    void DisconnectBlock(
        std::pair<CBlock,bool>& disconnectedBlockAndStatus,
        CValidationState& state,
        CBlockIndex* pindex,
        CCoinsViewCache& coins) const;
};
#endif// ACTIVE_CHAIN_MANAGER_H