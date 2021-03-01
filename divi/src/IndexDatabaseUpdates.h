#ifndef INDEX_DATABASE_UPDATES_H
#define INDEX_DATABASE_UPDATES_H
#include <vector>
#include <addressindex.h>
#include <spentindex.h>
#include <uint256.h>

struct IndexDatabaseUpdates
{
    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> > spentIndex;

    IndexDatabaseUpdates();
};

struct TransactionLocationReference
{
    uint256 hash;
    unsigned blockHeight;
    int transactionIndex;

    TransactionLocationReference(
        uint256 hashValue,
        unsigned blockheightValue,
        int transactionIndexValue);
};
#endif// INDEX_DATABASE_UPDATES_H