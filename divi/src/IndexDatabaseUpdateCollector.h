#ifndef INDEX_DATABASE_UPDATE_COLLECTOR_H
#define INDEX_DATABASE_UPDATE_COLLECTOR_H
#include <utility>
#include <uint256.h>

class CScript;
class CTransaction;
struct TransactionLocationReference;
class CCoinsViewCache;
struct IndexDatabaseUpdates;

class IndexDatabaseUpdateCollector
{
private:
    IndexDatabaseUpdateCollector(){};
public:
    static void RecordTransaction(
        const CTransaction& tx,
        const TransactionLocationReference& txLocationRef,
        const CCoinsViewCache& view,
        IndexDatabaseUpdates& indexDatabaseUpdates);
    static void ReverseTransaction(
        const CTransaction& tx,
        const TransactionLocationReference& txLocationReference,
        const CCoinsViewCache& view,
        IndexDatabaseUpdates& indexDBUpdates);
};
typedef std::pair<uint160,int> HashBytesAndAddressType;
HashBytesAndAddressType ComputeHashbytesAndAddressTypeForScript(const CScript& script);
#endif// INDEX_DATABASE_UPDATE_COLLECTOR_H