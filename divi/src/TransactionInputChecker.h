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

namespace boost
{
class thread_group;
} // namespace boost

class TransactionInputChecker
{
private:
    unsigned nSigOps;
    std::vector<CScriptCheck> vChecks;
    CCheckQueueControl<CScriptCheck> multiThreadedScriptChecker;
    const CCoinsViewCache& view_;
    const BlockMap& blockIndexMap_;
    CValidationState& state_;
    static int nScriptCheckThreads;

    static void ThreadScriptCheck();
public:
    static void SetScriptCheckingThreadCount(int threadCount);
    static int GetScriptCheckingThreadCount();
    static void InitializeScriptCheckingThreads(boost::thread_group& threadGroup);

    TransactionInputChecker(
        const CCoinsViewCache& view,
        const BlockMap& blockIndexMap,
        CValidationState& state);

    void ScheduleBackgroundThreadScriptChecking();
    bool CheckInputsAndUpdateCoinSupplyRecords(
        const CTransaction& tx,
        unsigned flags,
        CBlockIndex* pindex);

    bool InputsAreValid(const CTransaction& tx) const;
    bool TotalSigOpsAreBelowMaximum(const CTransaction& tx);
    bool WaitForScriptsToBeChecked();
};
#endif// TRANSACTION_INPUT_CHECKER_H
