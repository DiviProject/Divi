#ifndef I_PEER_SYNC_QUERY_SERVICE_h
#define I_PEER_SYNC_QUERY_SERVICE_h
#include <vector>
class CNode;
class I_PeerSyncQueryService
{
public:
    virtual ~I_PeerSyncQueryService(){}
    virtual std::vector<CNode*> GetSporkSyncedOrInboundNodes() const = 0;
};
#endif // I_PEER_SYNC_QUERY_SERVICE_h