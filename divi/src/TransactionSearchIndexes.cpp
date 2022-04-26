#include <TransactionSearchIndexes.h>

#include <txdb.h>
#include <Logging.h>

bool TransactionSearchIndexes::GetAddressIndex(
    const CBlockTreeDB* pblocktree,
    uint160 addressHash,
    int type,
    std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
    int start,
    int end)
{
    if (!pblocktree->GetAddressIndexing())
        return error("address index not enabled");

    if (!pblocktree->ReadAddressIndex(addressHash, type, addressIndex, start, end))
        return error("unable to get txids for address");

    return true;
}

bool TransactionSearchIndexes::GetAddressUnspent(
    const CBlockTreeDB* pblocktree,
    uint160 addressHash,
    int type,
    std::vector<std::pair<CAddressUnspentKey,CAddressUnspentValue> > &unspentOutputs)
{
    if (!pblocktree->GetAddressIndexing())
        return error("address index not enabled");

    if (!pblocktree->ReadAddressUnspentIndex(addressHash, type, unspentOutputs))
        return error("unable to get txids for address");

    return true;
}

bool TransactionSearchIndexes::GetSpentIndex(
    const CBlockTreeDB* pblocktree,
    const CSpentIndexKey &key,
    CSpentIndexValue &value)
{
    if (!pblocktree->GetSpentIndexing())
        return false;

    if (!pblocktree->ReadSpentIndex(key, value))
        return false;

    return true;
}
