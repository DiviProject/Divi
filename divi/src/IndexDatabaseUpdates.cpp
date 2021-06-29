#include <IndexDatabaseUpdates.h>

#include <primitives/transaction.h>

IndexDatabaseUpdates::IndexDatabaseUpdates(
    ): addressIndex()
    , addressUnspentIndex()
    , spentIndex()
    , txLocationData()
{
}

TransactionLocationReference::TransactionLocationReference(
    const CTransaction& tx,
    unsigned blockheightValue,
    int transactionIndexValue
    ): hash(tx.GetHash())
    , blockHeight(blockheightValue)
    , transactionIndex(transactionIndexValue)
{
}
