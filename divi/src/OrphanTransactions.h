#ifndef ORPHAN_TRANSACTIONS_H
#define ORPHAN_TRANSACTIONS_H
#include <net.h>
#include <uint256.h>
#include <set>
void CleanupOrphanTransactionCaches();
const std::set<uint256>& GetOrphanSpendingTransactionIds(const uint256& txHash);
const CTransaction& GetOrphanTransaction(const uint256& txHash, NodeId& peer);
bool OrphanTransactionIsKnown(const uint256& hash);
bool AddOrphanTx(const CTransaction& tx, NodeId peer);
void EraseOrphanTx(uint256 hash);
void EraseOrphansFor(NodeId peer);
unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans);
#endif// ORPHAN_TRANSACTIONS_H