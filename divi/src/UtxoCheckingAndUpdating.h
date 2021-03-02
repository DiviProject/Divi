#ifndef UTXO_CHECKING_AND_UPDATING_H
#define UTXO_CHECKING_AND_UPDATING_H
#include <vector>
#include <scriptCheck.h>
#include <amount.h>

class CTransaction;
class CValidationState;
class CCoinsViewCache;
class CTxUndo;
void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, CTxUndo& txundo, int nHeight);
bool CheckInputs(
    const CTransaction& tx,
    CValidationState& state,
    const CCoinsViewCache& inputs,
    bool fScriptChecks,
    unsigned int flags,
    bool cacheStore,
    std::vector<CScriptCheck>* pvChecks = nullptr);
bool CheckInputs(
    const CTransaction& tx,
    CValidationState& state,
    const CCoinsViewCache& inputs,
    CAmount& nFees,
    CAmount& nValueIn,
    bool fScriptChecks,
    unsigned int flags,
    bool cacheStore,
    std::vector<CScriptCheck>* pvChecks = nullptr,
    bool connectBlockDoS = false);
#endif// UTXO_CHECKING_AND_UPDATING_H