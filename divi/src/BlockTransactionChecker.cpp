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
#include <kernel.h>
#include <IndexDatabaseUpdateCollector.h>

TransactionLocationRecorder::TransactionLocationRecorder(
    const CBlockIndex* pindex,
    const CBlock& block
    ): nextBlockTxOnDiskLocation_(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()))
    , txLocationData_()
{
    txLocationData_.reserve(block.vtx.size());
}

void TransactionLocationRecorder::RecordTxLocationData(const CTransaction& tx)
{
    txLocationData_.emplace_back(tx.GetHash(), nextBlockTxOnDiskLocation_);
    nextBlockTxOnDiskLocation_.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
}
const std::vector<std::pair<uint256, CDiskTxPos> >& TransactionLocationRecorder::getTxLocationData() const
{
    return txLocationData_;
}

BlockTransactionChecker::BlockTransactionChecker(
    const CBlock& block,
    CValidationState& state,
    CBlockIndex* pindex,
    CCoinsViewCache& view,
    const int blocksToSkipChecksFor
    ): blockundo_(block.vtx.size() - 1)
    , block_(block)
    , state_(state)
    , pindex_(pindex)
    , view_(view)
    , txInputChecker_(pindex->nHeight >= blocksToSkipChecksFor,view_,state_)
    , txLocationRecorder_(pindex_,block_)
{
}

bool BlockTransactionChecker::Check(const CBlockRewards& nExpectedMint,bool fJustCheck, IndexDatabaseUpdates& indexDatabaseUpdates)
{
    const CAmount nMoneySupplyPrev = pindex_->pprev ? pindex_->pprev->nMoneySupply : 0;
    pindex_->nMoneySupply = nMoneySupplyPrev;
    pindex_->nMint = 0;
    for (unsigned int i = 0; i < block_.vtx.size(); i++) {
        const CTransaction& tx = block_.vtx[i];
        const TransactionLocationReference txLocationRef(tx.GetHash(),pindex_->nHeight,i);

        if(!txInputChecker_.TotalSigOpsAreBelowMaximum(tx))
        {
            return false;
        }
        if (!tx.IsCoinBase())
        {
            if(!txInputChecker_.CheckInputsAndUpdateCoinSupplyRecords(tx,fJustCheck,pindex_))
            {
                return false;
            }
            txInputChecker_.ScheduleBackgroundThreadScriptChecking();
        }
        if (!CheckCoinstakeForVaults(tx, nExpectedMint, view_)) {
            return state_.DoS(100, error("%s : coinstake is invalid for vault",__func__),
                            REJECT_INVALID, "bad-coinstake-vault-spend");
        }

        IndexDatabaseUpdateCollector::RecordTransaction(tx,txLocationRef,view_, indexDatabaseUpdates);
        UpdateCoins(tx, view_, blockundo_.vtxundo[i>0u? i-1: 0u], pindex_->nHeight);
        txLocationRecorder_.RecordTxLocationData(tx);
    }
    indexDatabaseUpdates.txLocationData = txLocationRecorder_.getTxLocationData();
    return true;
}

bool BlockTransactionChecker::WaitForScriptsToBeChecked()
{
    return txInputChecker_.WaitForScriptsToBeChecked();
}

const std::vector<std::pair<uint256, CDiskTxPos> >& BlockTransactionChecker::getTxLocationData() const
{
    return txLocationRecorder_.getTxLocationData();
}

CBlockUndo& BlockTransactionChecker::getBlockUndoData()
{
    return blockundo_;
}