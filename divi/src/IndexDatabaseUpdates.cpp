#include <IndexDatabaseUpdates.h>

#include <primitives/transaction.h>

IndexDatabaseUpdates::IndexDatabaseUpdates(
    ): addressIndex()
    , addressUnspentIndex()
    , spentIndex()
    , txLocationData()
{
}