#ifndef TRANSACTION_SEARCH_INDEXES_H
#define TRANSACTION_SEARCH_INDEXES_H
#include <uint256.h>
#include <addressindex.h>
#include <spentindex.h>
class CBlockTreeDB;
namespace TransactionSearchIndexes
{
    bool GetAddressIndex(
        bool addresIndexEnabled,
        const CBlockTreeDB* pblocktree,
        uint160 addressHash,
        int type,
        std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
        int start = 0,
        int end = 0);
    bool GetAddressUnspent(
        bool addresIndexEnabled,
        const CBlockTreeDB* pblocktree,
        uint160 addressHash,
        int type,
        std::vector<std::pair<CAddressUnspentKey,CAddressUnspentValue> > &unspentOutputs);
    bool GetSpentIndex(
        bool spentIndexEnabled,
        const CBlockTreeDB* pblocktree,
        const CSpentIndexKey &key,
        CSpentIndexValue &value);
}
#endif// TRANSACTION_SEARCH_INDEXES_H
