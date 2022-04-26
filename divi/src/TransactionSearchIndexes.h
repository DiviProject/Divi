#ifndef TRANSACTION_SEARCH_INDEXES_H
#define TRANSACTION_SEARCH_INDEXES_H
#include <uint256.h>
#include <addressindex.h>
#include <spentindex.h>
class CBlockTreeDB;
namespace TransactionSearchIndexes
{
    bool GetAddressIndex(
        const CBlockTreeDB* pblocktree,
        uint160 addressHash,
        int type,
        std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
        int start = 0,
        int end = 0);
    bool GetAddressUnspent(
        const CBlockTreeDB* pblocktree,
        uint160 addressHash,
        int type,
        std::vector<std::pair<CAddressUnspentKey,CAddressUnspentValue> > &unspentOutputs);
    bool GetSpentIndex(
        const CBlockTreeDB* pblocktree,
        const CSpentIndexKey &key,
        CSpentIndexValue &value);
}
#endif// TRANSACTION_SEARCH_INDEXES_H
