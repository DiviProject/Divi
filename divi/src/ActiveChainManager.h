#ifndef ACTIVE_CHAIN_MANAGER_H
#define ACTIVE_CHAIN_MANAGER_H
class CBlock;
class CValidationState;
class CBlockIndex;
class CCoinsViewCache;
class CBlockTreeDB;
struct IndexDatabaseUpdates;
class ActiveChainManager
{
private:
    const bool& addressIndexingIsEnabled_;
    const bool& spentInputIndexingIsEnabled_;
    CBlockTreeDB* blocktree_;
public:
    ActiveChainManager(const bool& addressIndexingIsEnabled, const bool& spentInputIndexingIsEnabled, CBlockTreeDB* blocktree);
    bool DisconnectBlock(CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& coins, bool* pfClean = nullptr) const;
};
#endif// ACTIVE_CHAIN_MANAGER_H