#ifndef ACTIVE_CHAIN_MANAGER_H
#define ACTIVE_CHAIN_MANAGER_H
class CBlock;
class CValidationState;
class CBlockIndex;
class CCoinsViewCache;
class CBlockTreeDB;
struct IndexDatabaseUpdates;
class CTxIn;
class uint256;
class CTxInUndo;
struct TransactionLocationReference;
class ActiveChainManager
{
private:
    const bool& addressIndexingIsEnabled_;
    const bool& spentInputIndexingIsEnabled_;
    CBlockTreeDB* blocktree_;

    bool UpdateIndexDBs(
        IndexDatabaseUpdates& indexDBUpdates,
        CValidationState& state) const;
    void CollectIndexUpdatesFromInputs(
        CCoinsViewCache& view,
        const CTxIn& input,
        const TransactionLocationReference& txLocationReference,
        const uint256& hash,
        CBlockIndex* pindex,
        const int transactionIndex,
        const int txOutputIndex,
        const CTxInUndo& undo,
        IndexDatabaseUpdates& indexDBUpdates) const;
public:
    ActiveChainManager(const bool& addressIndexingIsEnabled, const bool& spentInputIndexingIsEnabled, CBlockTreeDB* blocktree);
    bool DisconnectBlock(CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& coins, bool* pfClean = nullptr) const;
};
#endif// ACTIVE_CHAIN_MANAGER_H