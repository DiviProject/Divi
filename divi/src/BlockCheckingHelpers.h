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
private:
    const bool compareByWorkOnly_;
public:
    CBlockIndexWorkComparator(bool defaultComparisonByWorkOnly = false): compareByWorkOnly_(defaultComparisonByWorkOnly) {}
    bool operator()(const CBlockIndex* pa, const CBlockIndex* pb) const
    {
        // First sort by most total work, ...
        if (pa->nChainWork > pb->nChainWork) return false;
        if (pa->nChainWork < pb->nChainWork) return true;
        if(compareByWorkOnly_) return true;

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
typedef std::multimap<CBlockIndex*, CBlockIndex*> BlockIndexSuccessorsByPreviousBlockIndex;
bool CheckBlock(const CBlock& block, CValidationState& state);
bool CheckTransaction(const CTransaction& tx, CValidationState& state);
bool CheckTransaction(const CTransaction& tx, CValidationState& state, std::set<COutPoint>& usedInputsSet);
void VerifyBlockIndexTree(
    const ChainstateManager& chainstate,
    CCriticalSection& mainCriticalSection,
    BlockIndexSuccessorsByPreviousBlockIndex& blockSuccessorsByPrevBlockIndex,
    BlockIndexCandidates& blockIndexCandidates);

/**
 * Return the tip of the chain with the most work in it, that isn't
 * known to be invalid (it's however far from certain to be valid).
 */
CBlockIndex* FindMostWorkChain(
    const ChainstateManager& chainstate,
    BlockIndexSuccessorsByPreviousBlockIndex& blockSuccessorsByPrevBlockIndex,
    BlockIndexCandidates& blockIndexCandidates);
uint256 getMostWorkForInvalidBlockIndex();
void updateMostWorkInvalidBlockIndex(const CBlockIndex* invalidBlockIndex, bool reconsider = false);
#endif// BLOCK_CHECKING_HELPERS_H