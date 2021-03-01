#ifndef INDEX_DATABASE_UPDATES_H
#define INDEX_DATABASE_UPDATES_H
#include <vector>
#include <addressindex.h>
#include <spentindex.h>
struct IndexDatabaseUpdates
{
    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> > spentIndex;

    IndexDatabaseUpdates();
};
#endif// INDEX_DATABASE_UPDATES_H