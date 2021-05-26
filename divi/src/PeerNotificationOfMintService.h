#ifndef PEER_NOTIFICATION_OF_MINT_SERVICE_H
#define PEER_NOTIFICATION_OF_MINT_SERVICE_H
#include <vector>
#include <I_PeerBlockNotifyService.h>
#include <sync.h>
class CNode;
class uint256;

class PeerNotificationOfMintService: public I_PeerBlockNotifyService
{
private:
    std::vector<CNode*>& peers_;
    CCriticalSection& peersLock_;
public:
    PeerNotificationOfMintService(std::vector<CNode*>& peers,CCriticalSection& peersLock);
    bool havePeersToNotify() const;
    void notifyPeers(const uint256& blockHash) const;
};

#endif // PEER_NOTIFICATION_OF_MINT_SERVICE_H