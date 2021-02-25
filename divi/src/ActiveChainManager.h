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
class ActiveChainManager
{
private:
    const bool& addressIndexingIsEnabled_;
    CBlockTreeDB* blocktree_;

    bool ApplyUpdateIndexDBs(
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
    bool UpdateDBIndices(
        CBlock& block,
        CBlockUndo& blockUndo,
        CValidationState& state,
        CBlockIndex* pindex,
        CCoinsViewCache& view) const;
    bool RecoverBlockUndoData(
        const CBlockIndex* pindex,
        CBlockUndo& blockUndo) const;
public:
    ActiveChainManager(const bool& addressIndexingIsEnabled, CBlockTreeDB* blocktree);
    bool DisconnectBlock(
        CBlock& block,
        CValidationState& state,
        CBlockIndex* pindex,
        CCoinsViewCache& coins,
        bool* pfClean = nullptr) const;
    std::pair<CBlock,bool> DisconnectBlock(
            CValidationState& state,
            CBlockIndex* pindex,
            CCoinsViewCache& coins) const;
};
#endif// ACTIVE_CHAIN_MANAGER_H