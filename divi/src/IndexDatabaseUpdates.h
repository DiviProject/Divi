#ifndef INDEX_DATABASE_UPDATES_H
#define INDEX_DATABASE_UPDATES_H
#include <vector>
#include <utility>
#include <addressindex.h>
#include <spentindex.h>
#include <uint256.h>
#include <TransactionLocationReference.h>

class CTransaction;

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
    const CBlockIndex* const blockIndex_;
    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> > spentIndex;
    std::vector<TxIndexEntry> txLocationData;
    const bool addressIndexingEnabled_;
    const bool spentIndexingEnabled_;

    IndexDatabaseUpdates(const CBlockIndex* const blockIndex, bool addressIndexingEnabled, bool spentIndexingEnabled);
};
#endif// INDEX_DATABASE_UPDATES_H
