#ifndef INDEX_DATABASE_UPDATES_H
#define INDEX_DATABASE_UPDATES_H
#include <vector>
#include <utility>
#include <addressindex.h>
#include <spentindex.h>
#include <uint256.h>

/** One entry in the tx index, which locates transactions on disk by their txid
 *  or bare txid (both keys are possible).  */
struct TxIndexEntry
{
    uint256 txid;
    uint256 bareTxid;
    CDiskTxPos diskPos;

    explicit TxIndexEntry(const uint256& t, const uint256& b, const CDiskTxPos& p)
      : txid(t), bareTxid(b), diskPos(p)
    {}
};

struct IndexDatabaseUpdates
{
    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> > spentIndex;
    std::vector<TxIndexEntry> txLocationData;

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
