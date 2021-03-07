#ifndef BLOCK_TRANSACTION_CHECKER_H
#define BLOCK_TRANSACTION_CHECKER_H

#include <addressindex.h>
#include <vector>
#include <uint256.h>
#include <BlockUndo.h>
#include  <TransactionInputChecker.h>
#include <IndexDatabaseUpdates.h>

class CBlockIndex;
class CBlock;
class CValidationState;
class CCoinsViewCache;
class CBlockRewards;

class TransactionLocationRecorder
{
private:
    CDiskTxPos nextBlockTxOnDiskLocation_;
    size_t numberOfTransactions_;
    bool txLocationDataSizeHasBeenPreallocated_;
public:

    TransactionLocationRecorder(
        const CBlockIndex* pindex,
        const CBlock& block);
    void RecordTxLocationData(
        const CTransaction& tx,
        std::vector<std::pair<uint256, CDiskTxPos> >& txLocationData);
};

class BlockTransactionChecker
{
private:
    CBlockUndo blockundo_;
    const CBlock& block_;
    CValidationState& state_;
    CBlockIndex* pindex_;
    CCoinsViewCache& view_;
    TransactionInputChecker txInputChecker_;
    TransactionLocationRecorder txLocationRecorder_;
public:
    BlockTransactionChecker(
        const CBlock& block,
        CValidationState& state,
        CBlockIndex* pindex,
        CCoinsViewCache& view,
        const int blocksToSkipChecksFor);

    bool Check(
        const CBlockRewards& nExpectedMint,
        bool fJustCheck,
        IndexDatabaseUpdates& indexDatabaseUpdates);
    bool WaitForScriptsToBeChecked();
    CBlockUndo& getBlockUndoData();
};

#endif// BLOCK_TRANSACTION_CHECKER_H