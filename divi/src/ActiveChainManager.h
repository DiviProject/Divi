#ifndef ACTIVE_CHAIN_MANAGER_H
#define ACTIVE_CHAIN_MANAGER_H
class CBlock;
class CValidationState;
class CBlockIndex;
class CCoinsViewCache;
class CBlockTreeDB;
class ActiveChainManager
{
private:
    const bool& addressIndexingIsEnabled_;
    const bool& spentInputIndexingIsEnabled_;
public:
    ActiveChainManager(const bool& addressIndexingIsEnabled, const bool& spentInputIndexingIsEnabled);
    static bool DisconnectBlock(CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& coins, CBlockTreeDB* pblocktree, bool* pfClean = nullptr);
};
#endif// ACTIVE_CHAIN_MANAGER_H