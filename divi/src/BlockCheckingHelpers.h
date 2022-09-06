#ifndef BLOCK_CHECKING_HELPERS_H
#define BLOCK_CHECKING_HELPERS_H
#include <primitives/transaction.h>
#include <set>
#include <map>
#include <chain.h>
#include <BlockIndexWork.h>
class CBlock;
class CTransaction;
class CValidationState;
class CCriticalSection;
class CBlockIndex;
class ChainstateManager;

bool CheckBlock(const CBlock& block, CValidationState& state);
/** Context-independent validity checks */
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