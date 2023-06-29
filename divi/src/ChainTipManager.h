#ifndef CHAIN_TIP_MANGER_H
#define CHAIN_TIP_MANGER_H
#include <map>
#include <memory>

#include <I_ChainTipManager.h>
#include <uint256.h>
#include <NodeId.h>

class CSporkManager;
class ChainstateManager;
class CValidationState;
class Settings;
class CCriticalSection;
class CTxMemPool;
class MainNotificationSignals;
class I_BlockDataReader;
class BlockConnectionService;
class CChainParams;
class I_SuperblockSubsidyContainer;
class I_BlockIncentivesPopulator;

class ChainTipManager final: public I_ChainTipManager
{
private:
    const Settings& settings_;
    CCriticalSection& mainCriticalSection_;
    CTxMemPool& mempool_;
    MainNotificationSignals& mainNotificationSignals_;
    std::map<uint256, NodeId>& peerIdByBlockHash_;
    const CSporkManager& sporkManager_;
    ChainstateManager& chainstate_;
    std::unique_ptr<I_BlockDataReader> blockDiskReader_;
    std::unique_ptr<const BlockConnectionService> blockConnectionService_;
public:
    ChainTipManager(
        const CChainParams& chainParameters,
        const Settings& settings,
        const I_SuperblockSubsidyContainer& blockSubsidies,
        const I_BlockIncentivesPopulator& incentives,
        CCriticalSection& mainCriticalSection,
        CTxMemPool& mempool,
        MainNotificationSignals& mainNotificationSignals,
        std::map<uint256, NodeId>& peerIdByBlockHash,
        const CSporkManager& sporkManager,
        ChainstateManager& chainstate);
    ~ChainTipManager();
    bool connectTip(CValidationState& state, const CBlock* pblock, CBlockIndex* blockIndex) const override;
    bool disconnectTip(CValidationState& state, const bool updateCoinDatabaseOnly) const override;
};
#endif// CHAIN_TIP_MANGER_H