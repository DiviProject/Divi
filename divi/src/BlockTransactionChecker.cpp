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

extern bool fAddressIndex;
extern bool fSpentIndex;

void UpdateSpendingActivityInIndexDatabaseUpdates(
    const CTransaction& tx,
    const TransactionLocationReference& txLocationRef,
    const CCoinsViewCache& view,
    IndexDatabaseUpdates& indexDatabaseUpdates)
{
    if (tx.IsCoinBase()) return;
    if (fAddressIndex || fSpentIndex)
    {
        for (size_t j = 0; j < tx.vin.size(); j++) {

            const CTxIn input = tx.vin[j];
            const CTxOut &prevout = view.GetOutputFor(tx.vin[j]);
            uint160 hashBytes;
            int addressType;

            if (prevout.scriptPubKey.IsPayToScriptHash()) {
                hashBytes = uint160(std::vector<unsigned char>(prevout.scriptPubKey.begin()+2, prevout.scriptPubKey.begin()+22));
                addressType = 2;
            } else if (prevout.scriptPubKey.IsPayToPublicKeyHash()) {
                hashBytes = uint160(std::vector<unsigned char>(prevout.scriptPubKey.begin()+3, prevout.scriptPubKey.begin()+23));
                addressType = 1;
            } else {
                hashBytes.SetNull();
                addressType = 0;
            }
            if (fAddressIndex && addressType > 0) {
                // record spending activity
                indexDatabaseUpdates.addressIndex.push_back(std::make_pair(CAddressIndexKey(addressType, hashBytes, txLocationRef.blockHeight, txLocationRef.transactionIndex, txLocationRef.hash, j, true), prevout.nValue * -1));
                // remove address from unspent index
                indexDatabaseUpdates.addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(addressType, hashBytes, input.prevout.hash, input.prevout.n), CAddressUnspentValue()));
            }
            if (fSpentIndex) {
                // add the spent index to determine the txid and input that spent an output
                // and to find the amount and address from an input
                indexDatabaseUpdates.spentIndex.push_back(std::make_pair(CSpentIndexKey(input.prevout.hash, input.prevout.n), CSpentIndexValue(txLocationRef.hash, j, txLocationRef.blockHeight, prevout.nValue, addressType, hashBytes)));
            }
        }
    }
}

void UpdateNewOutputsInIndexDatabaseUpdates(
    const CTransaction& tx,
    const TransactionLocationReference& txLocationRef,
    IndexDatabaseUpdates& indexDatabaseUpdates)
{
    if (fAddressIndex) {
        for (unsigned int k = 0; k < tx.vout.size(); k++) {
            const CTxOut &out = tx.vout[k];

            if (out.scriptPubKey.IsPayToScriptHash()) {
                std::vector<unsigned char> hashBytes(out.scriptPubKey.begin()+2, out.scriptPubKey.begin()+22);

                // record receiving activity
                indexDatabaseUpdates.addressIndex.push_back(
                    std::make_pair(CAddressIndexKey(2, uint160(hashBytes), txLocationRef.blockHeight, txLocationRef.transactionIndex, txLocationRef.hash, k, false), out.nValue));

                // record unspent output
                indexDatabaseUpdates.addressUnspentIndex.push_back(
                    std::make_pair(CAddressUnspentKey(2, uint160(hashBytes), txLocationRef.hash, k), CAddressUnspentValue(out.nValue, out.scriptPubKey, txLocationRef.blockHeight)));

            } else if (out.scriptPubKey.IsPayToPublicKeyHash()) {
                std::vector<unsigned char> hashBytes(out.scriptPubKey.begin()+3, out.scriptPubKey.begin()+23);

                // record receiving activity
                indexDatabaseUpdates.addressIndex.push_back(
                    std::make_pair(CAddressIndexKey(1, uint160(hashBytes), txLocationRef.blockHeight, txLocationRef.transactionIndex, txLocationRef.hash, k, false), out.nValue));

                // record unspent output
                indexDatabaseUpdates.addressUnspentIndex.push_back(
                    std::make_pair(CAddressUnspentKey(1, uint160(hashBytes), txLocationRef.hash, k), CAddressUnspentValue(out.nValue, out.scriptPubKey, txLocationRef.blockHeight)));

            } else {
                continue;
            }

        }
    }
}




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
            return state_.DoS(100, error("ConnectBlock() : coinstake is invalid for vault"),
                            REJECT_INVALID, "bad-coinstake-vault-spend");
        }

        UpdateSpendingActivityInIndexDatabaseUpdates(tx,txLocationRef,view_, indexDatabaseUpdates);
        UpdateNewOutputsInIndexDatabaseUpdates(tx,txLocationRef,indexDatabaseUpdates);
        UpdateCoins(tx, view_, blockundo_.vtxundo[i>0u? i-1: 0u], pindex_->nHeight);
        txLocationRecorder_.RecordTxLocationData(tx);
    }
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