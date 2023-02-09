#include <IndexDatabaseUpdates.h>

#include <primitives/transaction.h>

IndexDatabaseUpdates::IndexDatabaseUpdates(
    const CBlockIndex* const blockIndex,
    bool addressIndexingEnabled,
    bool spentIndexingEnabled
    ): blockIndex_(blockIndex)
    , addressIndex()
    , addressUnspentIndex()
    , spentIndex()
    , txLocationData()
    , addressIndexingEnabled_(addressIndexingEnabled)
    , spentIndexingEnabled_(spentIndexingEnabled)
{
}