#include <TransactionInputChecker.h>

#include <coins.h>
#include <ValidationState.h>
#include <chain.h>
#include <UtxoCheckingAndUpdating.h>
#include <TransactionOpCounting.h>
#include <primitives/transaction.h>
#include <Logging.h>
#include <defaultValues.h>
#include <ThreadManagementHelpers.h>

extern int nScriptCheckThreads;
static CCheckQueue<CScriptCheck> scriptcheckqueue(128);

void TransactionInputChecker::ThreadScriptCheck()
{
    RenameThread("divi-scriptch");
    scriptcheckqueue.Thread();
}

TransactionInputChecker::TransactionInputChecker(
    const CCoinsViewCache& view,
    const BlockMap& blockIndexMap,
    CValidationState& state
    ): nSigOps(0u)
    , vChecks()
    , multiThreadedScriptChecker( nScriptCheckThreads ? &scriptcheckqueue : NULL )
    , view_(view)
    , blockIndexMap_(blockIndexMap)
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
    const unsigned flags,
    CBlockIndex* pindex)
{
    assert(vChecks.empty());
    CAmount txFees =0;
    CAmount txInputAmount=0;
    if (!CheckInputs(tx, state_, view_, blockIndexMap_, txFees, txInputAmount, true, flags, nScriptCheckThreads ? &vChecks : NULL, true))
    {
        vChecks.clear();
        return false;
    }

    const CAmount supplyChange = tx.GetValueOut() - txInputAmount;
    pindex->nMoneySupply += supplyChange;
    pindex->nMint += (tx.IsCoinBase() || tx.IsCoinStake())? supplyChange : 0;
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

bool TransactionInputChecker::InputsAreValid(const CTransaction& tx) const
{
    return view_.HaveInputs(tx);
}
