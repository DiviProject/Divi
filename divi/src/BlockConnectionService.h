#ifndef ACTIVE_CHAIN_MANAGER_H
#define ACTIVE_CHAIN_MANAGER_H
#include <primitives/block.h>
#include <utility>
#include <memory>

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
class CSporkManager;
class BlockMap;
class SuperblockSubsidyContainer;
class BlockIncentivesPopulator;
class MasternodeModule;
class CChainParams;

class BlockConnectionService
{
private:
    const bool addressIndexingIsEnabled_;
    const bool spentIndexingIsEnabled_;
    const BlockMap& blockIndicesByHash_;
    CBlockTreeDB* blocktree_;
    CCoinsViewCache* const coinTip_;
    const CSporkManager& sporkManager_;
    const I_BlockDataReader& blockDataReader_;
    const bool modifyCoinCacheInplace_;
    const CChainParams& chainParameters_;
    std::unique_ptr<const SuperblockSubsidyContainer> blockSubsidies_;
    std::unique_ptr<const BlockIncentivesPopulator> incentives_;

    bool ApplyDisconnectionUpdateIndexToDBs(
        const uint256& bestBlockHash,
        IndexDatabaseUpdates& indexDBUpdates,
        CValidationState& state) const;

    bool DisconnectBlock(
        const CBlock& block,
        CValidationState& state,
        const CBlockIndex* pindex,
        CCoinsViewCache& coins,
        bool fJustCheck) const;
public:
    BlockConnectionService(
        const CChainParams& chainParameters,
        const MasternodeModule& masternodeModule,
        const BlockMap& blockIndicesByHash,
        CBlockTreeDB* blocktree,
        CCoinsViewCache* coinTip,
        const CSporkManager& sporkManager,
        const I_BlockDataReader& blockDataReader,
        const bool modifyCoinCacheInplace);

    ~BlockConnectionService();
    /** Disconnects a block given by pindex, which is also first loaded from
     *  disk and returned as part of disconnectedBlockAndStatus.
     *  This method always fully disconnects (i.e. fJustCheck=false).  */
    std::pair<CBlock,bool> DisconnectBlock(
        CValidationState& state,
        const CBlockIndex* pindex,
        const bool updateCoinsCacheOnly) const;
};
#endif// ACTIVE_CHAIN_MANAGER_H
