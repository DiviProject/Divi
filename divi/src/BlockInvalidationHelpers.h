#ifndef BLOCK_INVALIDATION_HELPERS_H
#define BLOCK_INVALIDATION_HELPERS_H
#include <uint256.h>
#include <NodeId.h>
#include <map>
#include <BlockCheckingHelpers.h>

class ChainstateManager;
class CValidationState;
class CBlockIndex;
class Settings;
class CCriticalSection;
class I_ChainTipManager;
class CChain;

BlockIndexCandidates& GetBlockIndexCandidates();
void PruneBlockIndexCandidates(const CChain& chain);
void InvalidChainFound(
    const bool isInitialBlockDownload,
    const Settings& settings,
    CCriticalSection& mainCriticalSection,
    CBlockIndex* pindexNew);
bool InvalidateBlock(
    const I_ChainTipManager& chainTipManager,
    const bool isInitialBlockDownload,
    const Settings& settings,
    CCriticalSection& mainCriticalSection,
    ChainstateManager& chainstate,
    CBlockIndex* pindex);
void InvalidBlockFound(
    const std::map<uint256, NodeId>& peerIdByBlockHash,
    const bool isInitialBlockDownload,
    const Settings& settings,
    CCriticalSection& mainCriticalSection,
    CBlockIndex* pindex,
    const CValidationState& state);
#endif// BLOCK_INVALIDATION_HELPERS_H