#include <IndexDatabaseUpdates.h>

#include <primitives/transaction.h>

IndexDatabaseUpdates::IndexDatabaseUpdates(
    bool addressIndexingEnabled,
    bool spentIndexingEnabled
    ): addressIndex()
    , addressUnspentIndex()
    , spentIndex()
    , txLocationData()
    , addressIndexingEnabled_(addressIndexingEnabled)
    , spentIndexingEnabled_(spentIndexingEnabled)
{
}