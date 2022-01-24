#include <TransactionLocationReference.h>
#include <primitives/transaction.h>

TransactionLocationReference::TransactionLocationReference(
    const CTransaction& tx,
    unsigned blockheightValue,
    int transactionIndexValue
    ): hash(tx.GetHash())
    , blockHeight(blockheightValue)
    , transactionIndex(transactionIndexValue)
{
}
