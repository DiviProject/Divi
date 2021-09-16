#ifndef I_PEER_SYNC_QUERY_SERVICE_H
#define I_PEER_SYNC_QUERY_SERVICE_H
#include <vector>
#include <NodeRef.h>
class I_PeerSyncQueryService
{
public:
    virtual ~I_PeerSyncQueryService(){}
    virtual std::vector<NodeRef> GetSporkSyncedOrInboundNodes() const = 0;
};
#endif // I_PEER_SYNC_QUERY_SERVICE_H