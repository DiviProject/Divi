#include <TransactionSearchIndexes.h>

#include <txdb.h>
#include <Logging.h>

bool TransactionSearchIndexes::GetAddressIndex(
    bool addresIndexEnabled,
    CBlockTreeDB* pblocktree,
    uint160 addressHash,
    int type,
    std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
    int start,
    int end)
{
    if (!addresIndexEnabled)
        return error("address index not enabled");

    if (!pblocktree->ReadAddressIndex(addressHash, type, addressIndex, start, end))
        return error("unable to get txids for address");

    return true;
}

bool TransactionSearchIndexes::GetAddressUnspent(
    bool addresIndexEnabled,
    CBlockTreeDB* pblocktree,
    uint160 addressHash,
    int type,
    std::vector<std::pair<CAddressUnspentKey,CAddressUnspentValue> > &unspentOutputs)
{
    if (!addresIndexEnabled)
        return error("address index not enabled");

    if (!pblocktree->ReadAddressUnspentIndex(addressHash, type, unspentOutputs))
        return error("unable to get txids for address");

    return true;
}

bool TransactionSearchIndexes::GetSpentIndex(
    bool spentIndexEnabled,
    CBlockTreeDB* pblocktree,
    const CSpentIndexKey &key,
    CSpentIndexValue &value)
{
    if (!spentIndexEnabled)
        return false;

    if (!pblocktree->ReadSpentIndex(key, value))
        return false;

    return true;
}