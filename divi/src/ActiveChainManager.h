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