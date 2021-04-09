#include <OrphanTransactions.h>

#include <Logging.h>
#include <uint256.h>
#include <set>
#include <map>
#include <random.h>
#include <boost/foreach.hpp>
#include <primitives/transaction.h>
//////////////////////////////////////////////////////////////////////////////
//
// mapOrphanTransactions
//

struct COrphanTx {
    CTransaction tx;
    NodeId fromPeer;
};
std::map<uint256, COrphanTx> mapOrphanTransactions;
std::map<uint256, std::set<uint256> > mapOrphanTransactionsByPrev;


//////////////////////////////////////////////////////////////////////////////
//
// mapOrphanTransactions
//
const std::set<uint256>& GetOrphanSpendingTransactionIds(const uint256& txHash)
{
    static std::set<uint256> emptySet;
    std::map<uint256, std::set<uint256> >::const_iterator it = mapOrphanTransactionsByPrev.find(txHash);
    if(it == mapOrphanTransactionsByPrev.end()) return emptySet;

    return it->second;
}
const CTransaction& GetOrphanTransaction(const uint256& txHash, NodeId& peer)
{
    peer = mapOrphanTransactions[txHash].fromPeer;
    return mapOrphanTransactions[txHash].tx;;
}
bool OrphanTransactionIsKnown(const uint256& hash)
{
    return mapOrphanTransactions.count(hash) > 0;
}
bool AddOrphanTx(const CTransaction& tx, NodeId peer)
{
    uint256 hash = tx.GetHash();
    if (OrphanTransactionIsKnown(hash))
        return false;

    // Ignore big transactions, to avoid a
    // send-big-orphans memory exhaustion attack. If a peer has a legitimate
    // large transaction with a missing parent then we assume
    // it will rebroadcast it later, after the parent transaction(s)
    // have been mined or received.
    // 10,000 orphans, each of which is at most 5,000 bytes big is
    // at most 500 megabytes of orphans:
    unsigned int sz = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);
    if (sz > 5000) {
        LogPrint("mempool", "ignoring large orphan tx (size: %u, hash: %s)\n", sz, hash);
        return false;
    }

    mapOrphanTransactions[hash].tx = tx;
    mapOrphanTransactions[hash].fromPeer = peer;
    BOOST_FOREACH (const CTxIn& txin, tx.vin)
            mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);

    LogPrint("mempool", "stored orphan tx %s (mapsz %u prevsz %u)\n", hash,
             mapOrphanTransactions.size(), mapOrphanTransactionsByPrev.size());
    return true;
}

void EraseOrphanTx(uint256 hash)
{
    std::map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.find(hash);
    if (it == mapOrphanTransactions.end())
        return;
    BOOST_FOREACH (const CTxIn& txin, it->second.tx.vin) {
        std::map<uint256, std::set<uint256> >::iterator itPrev = mapOrphanTransactionsByPrev.find(txin.prevout.hash);
        if (itPrev == mapOrphanTransactionsByPrev.end())
            continue;
        itPrev->second.erase(hash);
        if (itPrev->second.empty())
            mapOrphanTransactionsByPrev.erase(itPrev);
    }
    mapOrphanTransactions.erase(it);
}

void EraseOrphansFor(NodeId peer)
{
    int nErased = 0;
    std::map<uint256, COrphanTx>::iterator iter = mapOrphanTransactions.begin();
    while (iter != mapOrphanTransactions.end()) {
        std::map<uint256, COrphanTx>::iterator maybeErase = iter++; // increment to avoid iterator becoming invalid
        if (maybeErase->second.fromPeer == peer) {
            EraseOrphanTx(maybeErase->second.tx.GetHash());
            ++nErased;
        }
    }
    if (nErased > 0) LogPrint("mempool", "Erased %d orphan tx from peer %d\n", nErased, peer);
}


unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans)
{
    unsigned int nEvicted = 0;
    while (mapOrphanTransactions.size() > nMaxOrphans) {
        // Evict a random orphan:
        uint256 randomhash = GetRandHash();
        std::map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
        if (it == mapOrphanTransactions.end())
            it = mapOrphanTransactions.begin();
        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}

const CTransaction& SelectRandomOrphan()
{
    uint256 randomhash = GetRandHash();
    std::map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
    if (it == mapOrphanTransactions.end())
        it = mapOrphanTransactions.begin();

    return it->second.tx;
}
size_t OrphanTotalCount()
{
    return mapOrphanTransactions.size();
}
bool OrphanMapsAreEmpty()
{
    return mapOrphanTransactions.empty() && mapOrphanTransactionsByPrev.empty();
}
