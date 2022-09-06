#ifndef BLOCK_INDEX_WORK_H
#define BLOCK_INDEX_WORK_H
#include <chain.h>
#include <set>
#include <map>
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
#endif// BLOCK_INDEX_WORK_H