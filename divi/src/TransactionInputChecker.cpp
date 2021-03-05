#include <TransactionInputChecker.h>

#include <coins.h>
#include <ValidationState.h>
#include <chain.h>
#include <UtxoCheckingAndUpdating.h>
#include <TransactionOpCounting.h>
#include <primitives/transaction.h>
#include <Logging.h>
#include <defaultValues.h>

extern int nScriptCheckThreads;
extern void RenameThread(const char* name);//in util.h
static CCheckQueue<CScriptCheck> scriptcheckqueue(128);

void TransactionInputChecker::ThreadScriptCheck()
{
    RenameThread("divi-scriptch");
    scriptcheckqueue.Thread();
}

TransactionInputChecker::TransactionInputChecker(
    bool checkScripts,
    const CCoinsViewCache& view,
    CValidationState& state
    ): nSigOps(0u)
    , fScriptChecks(checkScripts)
    , vChecks()
    , multiThreadedScriptChecker(fScriptChecks && nScriptCheckThreads ? &scriptcheckqueue : NULL )
    , view_(view)
    , state_(state)
{
}
void TransactionInputChecker::ScheduleBackgroundThreadScriptChecking()
{
    multiThreadedScriptChecker.Add(vChecks);
    vChecks.clear();
}
bool TransactionInputChecker::CheckInputsAndUpdateCoinSupplyRecords(
    const CTransaction& tx,
    const bool fJustCheck,
    CBlockIndex* pindex)
{
    assert(vChecks.empty());
    CAmount txFees =0;
    CAmount txInputAmount=0;
    if (!CheckInputs(tx, state_, view_, txFees, txInputAmount, fScriptChecks, MANDATORY_SCRIPT_VERIFY_FLAGS, fJustCheck, nScriptCheckThreads ? &vChecks : NULL, true))
    {
        vChecks.clear();
        return false;
    }

    const CAmount mintingMinusBurn = tx.GetValueOut() - txInputAmount;
    pindex->nMoneySupply += mintingMinusBurn;
    pindex->nMint += mintingMinusBurn + txFees;
    return true;
}

bool TransactionInputChecker::TotalSigOpsAreBelowMaximum(const CTransaction& tx)
{
    nSigOps += GetLegacySigOpCount(tx);
    if (nSigOps > MAX_BLOCK_SIGOPS_CURRENT)
        return state_.DoS(100, error("%s : too many sigops",__func__), REJECT_INVALID, "bad-blk-sigops");

    if (!tx.IsCoinBase())
    {
        // Add in sigops done by pay-to-script-hash inputs;
        // this is to prevent a "rogue miner" from creating
        // an incredibly-expensive-to-validate block.
        nSigOps += GetP2SHSigOpCount(tx, view_);
        if (nSigOps > MAX_BLOCK_SIGOPS_CURRENT)
            return state_.DoS(100, error("%s : too many sigops",__func__), REJECT_INVALID, "bad-blk-sigops");
    }
    return true;
}

bool TransactionInputChecker::WaitForScriptsToBeChecked()
{
    return multiThreadedScriptChecker.Wait();
}