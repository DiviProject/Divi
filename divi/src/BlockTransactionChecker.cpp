#include <BlockTransactionChecker.h>

#include <chain.h>
#include <primitives/block.h>
#include <serialize.h>
#include <amount.h>
#include <coins.h>
#include <Logging.h>
#include <defaultValues.h>
#include <ValidationState.h>
#include <clientversion.h>
#include <BlockRewards.h>
#include <UtxoCheckingAndUpdating.h>
#include <IndexDatabaseUpdateCollector.h>
#include <script/StakingVaultScript.h>
#include <utilmoneystr.h>

TransactionLocationRecorder::TransactionLocationRecorder(
    const CBlockIndex* pindex,
    const CBlock& block
    ): nextBlockTxOnDiskLocation_(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()))
    , numberOfTransactions_(block.vtx.size())
    , txLocationDataSizeHasBeenPreallocated_(false)
{
}

void TransactionLocationRecorder::RecordTxLocationData(
    const CTransaction& tx,
    std::vector<TxIndexEntry>& txLocationData)
{
    if(!txLocationDataSizeHasBeenPreallocated_)
    {
        txLocationData.reserve(numberOfTransactions_);
        txLocationDataSizeHasBeenPreallocated_ = true;
    }
    txLocationData.emplace_back(tx.GetHash(), tx.GetBareTxid(), nextBlockTxOnDiskLocation_);
    nextBlockTxOnDiskLocation_.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
}

BlockTransactionChecker::BlockTransactionChecker(
    const CBlock& block,
    CValidationState& state,
    CBlockIndex* pindex,
    CCoinsViewCache& view,
    const BlockMap& blockIndexMap
    ): blockundo_(block.vtx.size() - 1)
    , block_(block)
    , activation_(pindex)
    , state_(state)
    , pindex_(pindex)
    , view_(view)
    , txInputChecker_(view_,blockIndexMap,state_)
    , txLocationRecorder_(pindex_,block_)
{
}

bool BlockTransactionChecker::CheckCoinstakeForVaults(
    const CTransaction& tx,
    const CBlockRewards& expectedRewards,
    const CCoinsViewCache& view)
{
    if (!tx.IsCoinStake())
        return true;

    CAmount nValueIn = 0;
    bool foundVault = false;
    CScript vaultScript;
    for (const auto& in : tx.vin) {
        const auto& prevOut = view.GetOutputFor(in);
        nValueIn += prevOut.nValue;
        if (!IsStakingVaultScript(prevOut.scriptPubKey))
            continue;

        if (foundVault) {
            /* CheckProofOfStake already verifies that all inputs used are
               from a single script.  */
            if(vaultScript != prevOut.scriptPubKey) return false;
        } else {
            foundVault = true;
            vaultScript = prevOut.scriptPubKey;
        }
    }

    if (!foundVault)
        return true;

    assert(tx.vout.size() >= 2);
    const auto& rewardOut = tx.vout[1];
    if (rewardOut.scriptPubKey != vaultScript)
        return error("%s: output is not sent back to the vault input script", __func__);
    CAmount actualOutput = rewardOut.nValue;

    /* We optionally allow splitting of the output into two (but not more),
       provided that both have a value >= 10k DIVI.  */
    constexpr CAmount MIN_FOR_SPLITTING = 10000 * COIN;
    if (tx.vout.size() >= 3) {
        const auto& out2 = tx.vout[2];
        if (actualOutput >= MIN_FOR_SPLITTING && out2.nValue >= MIN_FOR_SPLITTING
              && out2.scriptPubKey == vaultScript)
            actualOutput += out2.nValue;
    }

    const CAmount expectedOutput = nValueIn + expectedRewards.nStakeReward;
    if (actualOutput < expectedOutput)
        return error("%s: expected output to be at least %s, got only %s",
                     __func__, FormatMoney(expectedOutput), FormatMoney(actualOutput));

    return true;
}

bool BlockTransactionChecker::Check(const CBlockRewards& nExpectedMint, IndexDatabaseUpdates& indexDatabaseUpdates)
{
    const CAmount nMoneySupplyPrev = pindex_->pprev ? pindex_->pprev->nMoneySupply : 0;
    pindex_->nMoneySupply = nMoneySupplyPrev;
    pindex_->nMint = 0;

    unsigned flags = MANDATORY_SCRIPT_VERIFY_FLAGS;
    if (activation_.IsActive(Fork::CheckLockTimeVerify))
        flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;

    for (unsigned int i = 0; i < block_.vtx.size(); i++) {
        const CTransaction& tx = block_.vtx[i];
        const TransactionLocationReference txLocationRef(tx, pindex_->nHeight, i);

        if(!txInputChecker_.InputsAreValid(tx))
        {
            return state_.DoS(100,
                error("%s: tx has invalid input",__func__), REJECT_INVALID, "bad-txn-unknown-inputs");
        }
        if(!txInputChecker_.TotalSigOpsAreBelowMaximum(tx))
        {
            return false;
        }
        if(!txInputChecker_.CheckInputsAndUpdateCoinSupplyRecords(tx, flags, pindex_))
        {
            return false;
        }
        if (!tx.IsCoinBase())
        {
            txInputChecker_.ScheduleBackgroundThreadScriptChecking();
        }
        if (!CheckCoinstakeForVaults(tx, nExpectedMint, view_)) {
            return state_.DoS(100, error("%s : coinstake is invalid for vault",__func__),
                            REJECT_INVALID, "bad-coinstake-vault-spend");
        }

        IndexDatabaseUpdateCollector::RecordTransaction(tx,txLocationRef,view_, indexDatabaseUpdates);
        view_.UpdateWithConfirmedTransaction(tx,pindex_->nHeight, blockundo_.vtxundo[i>0u? i-1: 0u]);
        txLocationRecorder_.RecordTxLocationData(tx,indexDatabaseUpdates.txLocationData);
    }
    return true;
}

bool BlockTransactionChecker::WaitForScriptsToBeChecked()
{
    return txInputChecker_.WaitForScriptsToBeChecked();
}

CBlockUndo& BlockTransactionChecker::getBlockUndoData()
{
    return blockundo_;
}
