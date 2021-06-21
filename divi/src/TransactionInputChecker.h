#ifndef TRANSACTION_INPUT_CHECKER_H
#define TRANSACTION_INPUT_CHECKER_H
#include <scriptCheck.h>
#include <checkqueue.h>
#include <vector>

class BlockMap;
class CValidationState;
class CCoinsViewCache;
class CBlockIndex;
class CTransaction;

class TransactionInputChecker
{
private:
    unsigned nSigOps;
    const bool fScriptChecks;
    std::vector<CScriptCheck> vChecks;
    CCheckQueueControl<CScriptCheck> multiThreadedScriptChecker;
    const CCoinsViewCache& view_;
    const BlockMap& blockIndexMap_;
    CValidationState& state_;

public:
    static void ThreadScriptCheck();
    TransactionInputChecker(
        bool checkScripts,
        const CCoinsViewCache& view,
        const BlockMap& blockIndexMap,
        CValidationState& state);

    void ScheduleBackgroundThreadScriptChecking();
    bool CheckInputsAndUpdateCoinSupplyRecords(
        const CTransaction& tx,
        const bool fJustCheck,
        CBlockIndex* pindex);
    bool TotalSigOpsAreBelowMaximum(const CTransaction& tx);
    bool WaitForScriptsToBeChecked();
};
#endif// TRANSACTION_INPUT_CHECKER_H
