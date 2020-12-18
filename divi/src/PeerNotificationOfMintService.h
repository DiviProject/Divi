#ifndef PEER_NOTIFICATION_OF_MINT_SERVICE_H
#define PEER_NOTIFICATION_OF_MINT_SERVICE_H
#include <vector>
class CNode;
class uint256;

class PeerNotificationOfMintService
{
private:
    std::vector<CNode*>& peers_;
public:
    PeerNotificationOfMintService(std::vector<CNode*>& peers);
    bool havePeersToNotify() const;
    void notifyPeers(const uint256& blockHash) const;
};

#endif // PEER_NOTIFICATION_OF_MINT_SERVICE_H