#ifndef UTXO_CHECKING_AND_UPDATING_H
#define UTXO_CHECKING_AND_UPDATING_H
#include <vector>
#include <scriptCheck.h>
#include <amount.h>

class BlockMap;
class CTransaction;
class CValidationState;
class CCoinsViewCache;
class CTxUndo;
class TransactionLocationReference;

bool CheckInputs(
    const CTransaction& tx,
    CValidationState& state,
    const CCoinsViewCache& inputs,
    const BlockMap& blockIndexMap,
    bool fScriptChecks,
    unsigned int flags,
    std::vector<CScriptCheck>* pvChecks = nullptr);
bool CheckInputs(
    const CTransaction& tx,
    CValidationState& state,
    const CCoinsViewCache& inputs,
    const BlockMap& blockIndexMap,
    CAmount& nFees,
    CAmount& nValueIn,
    bool fScriptChecks,
    unsigned int flags,
    std::vector<CScriptCheck>* pvChecks = nullptr,
    bool connectBlockDoS = false);
#endif// UTXO_CHECKING_AND_UPDATING_H
