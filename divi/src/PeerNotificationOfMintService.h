#ifndef PEER_NOTIFICATION_OF_MINT_SERVICE_H
#define PEER_NOTIFICATION_OF_MINT_SERVICE_H
#include <vector>
#include <I_PeerBlockNotifyService.h>
class CNode;
class uint256;

class PeerNotificationOfMintService: public I_PeerBlockNotifyService
{
private:
    std::vector<CNode*>& peers_;
public:
    PeerNotificationOfMintService(std::vector<CNode*>& peers);
    bool havePeersToNotify() const;
    void notifyPeers(const uint256& blockHash) const;
};

#endif // PEER_NOTIFICATION_OF_MINT_SERVICE_H