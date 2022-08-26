#ifndef BLOCK_CHECKING_HELPERS_H
#define BLOCK_CHECKING_HELPERS_H
#include <primitives/transaction.h>
#include <set>
#include <map>
#include <chain.h>
class CBlock;
class CTransaction;
class CValidationState;
class CCriticalSection;
class CBlockIndex;
class ChainstateManager;
struct CBlockIndexWorkComparator {
    bool operator()(const CBlockIndex* pa, const CBlockIndex* pb) const
    {
        // First sort by most total work, ...
        if (pa->nChainWork > pb->nChainWork) return false;
        if (pa->nChainWork < pb->nChainWork) return true;

        // ... then by earliest time received, ...
        if (pa->nSequenceId < pb->nSequenceId) return false;
        if (pa->nSequenceId > pb->nSequenceId) return true;

        // Use pointer address as tie breaker (should only happen with blocks
        // loaded from disk, as those all have id 0).
        if (pa < pb) return false;
        if (pa > pb) return true;

        // Identical blocks.
        return false;
    }
};
typedef std::set<CBlockIndex*, CBlockIndexWorkComparator> BlockIndexCandidates;
bool CheckBlock(const CBlock& block, CValidationState& state);
bool CheckTransaction(const CTransaction& tx, CValidationState& state);
bool CheckTransaction(const CTransaction& tx, CValidationState& state, std::set<COutPoint>& usedInputsSet);
void VerifyBlockIndexTree(
    const ChainstateManager& chainstate,
    CCriticalSection& mainCriticalSection,
    std::multimap<CBlockIndex*, CBlockIndex*>& mapBlocksUnlinked,
    BlockIndexCandidates& setBlockIndexCandidates);
#endif// BLOCK_CHECKING_HELPERS_H