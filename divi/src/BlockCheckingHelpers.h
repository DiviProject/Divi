#ifndef BLOCK_CHECKING_HELPERS_H
#define BLOCK_CHECKING_HELPERS_H
#include <primitives/transaction.h>
#include <set>
class CBlock;
class CTransaction;
class CValidationState;
bool CheckBlock(const CBlock& block, CValidationState& state);
bool CheckTransaction(const CTransaction& tx, CValidationState& state);
bool CheckTransaction(const CTransaction& tx, CValidationState& state, std::set<COutPoint>& usedInputsSet);
#endif// BLOCK_CHECKING_HELPERS_H