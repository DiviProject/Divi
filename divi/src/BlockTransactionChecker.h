#ifndef BLOCK_TRANSACTION_CHECKER_H
#define BLOCK_TRANSACTION_CHECKER_H

#include <addressindex.h>
#include <vector>
#include <uint256.h>
#include <BlockUndo.h>
#include <ForkActivation.h>
#include <TransactionInputChecker.h>
#include <IndexDatabaseUpdates.h>

class BlockMap;
class CBlockIndex;
class CBlock;
class CValidationState;
class CCoinsViewCache;
class CBlockRewards;

class TransactionLocationRecorder
{
private:
    size_t numberOfTransactions_;
    CDiskTxPos nextBlockTxOnDiskLocation_;
    bool txLocationDataSizeHasBeenPreallocated_;
public:

    TransactionLocationRecorder(
        const CBlockIndex* pindex,
        const size_t numberOfTransactions);
    void RecordTxLocationData(
        const CTransaction& tx,
        std::vector<TxIndexEntry>& txLocationData);
};

class BlockTransactionChecker
{
private:
    CBlockUndo blockundo_;
    const CBlock& block_;
    const ActivationState activation_;
    CValidationState& state_;
    CBlockIndex* pindex_;
    CCoinsViewCache& view_;
    TransactionInputChecker txInputChecker_;
    TransactionLocationRecorder txLocationRecorder_;

public:
    /** Checks if the transaction is a valid coinstake after the staking vault
     *  fork (which adds extra rules, like paying back at least the expected
     *  staking reward to the same script that the staking input came from).
     *  Note that the extra conditions only apply to actual stake inputs that
     *  are vault scripts; if the tx is a coinstake but the input is not a vault,
     *  then the fucntion just returns true without further checks.  */
    static bool CheckCoinstakeForVaults(
        const CTransaction& tx,
        const CBlockRewards& expectedRewards,
        const CCoinsViewCache& view);

    BlockTransactionChecker(
        const CBlock& block,
        CValidationState& state,
        CBlockIndex* pindex,
        CCoinsViewCache& view,
        const BlockMap& blockIndexMap);

    bool Check(
        const CBlockRewards& nExpectedMint,
        IndexDatabaseUpdates& indexDatabaseUpdates);
    bool WaitForScriptsToBeChecked();
    CBlockUndo& getBlockUndoData();
};

#endif// BLOCK_TRANSACTION_CHECKER_H
