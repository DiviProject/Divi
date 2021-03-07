#include <IndexDatabaseUpdates.h>

IndexDatabaseUpdates::IndexDatabaseUpdates(
    ): addressIndex()
    , addressUnspentIndex()
    , spentIndex()
    , txLocationData()
{
}

TransactionLocationReference::TransactionLocationReference(
    uint256 hashValue,
    unsigned blockheightValue,
    int transactionIndexValue
    ): hash(hashValue)
    , blockHeight(blockheightValue)
    , transactionIndex(transactionIndexValue)
{
}