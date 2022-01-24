#ifndef TRANSACTION_LOCATION_REFERENCE_H
#define TRANSACTION_LOCATION_REFERENCE_H
#include <uint256.h>
class CTransaction;
struct TransactionLocationReference
{
    uint256 hash;
    unsigned blockHeight;
    int transactionIndex;

    TransactionLocationReference(
        const CTransaction& tx,
        unsigned blockheightValue,
        int transactionIndexValue);
};
#endif// TRANSACTION_LOCATION_REFERENCE_H