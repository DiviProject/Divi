#ifndef I_BLOCKCHAIN_SYNC_QUERY_SERVICE_H
#define I_BLOCKCHAIN_SYNC_QUERY_SERVICE_H
class I_BlockchainSyncQueryService
{
public:
    virtual bool isBlockchainSynced() const = 0;
};
#endif// I_BLOCKCHAIN_SYNC_QUERY_SERVICE_H