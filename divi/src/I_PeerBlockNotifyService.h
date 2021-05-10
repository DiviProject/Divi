#ifndef I_PEER_BLOCK_NOTIFY_SERVICE_H
#define I_PEER_BLOCK_NOTIFY_SERVICE_H
class uint256;
class I_PeerBlockNotifyService
{
public:
    virtual ~I_PeerBlockNotifyService(){}
    virtual bool havePeersToNotify() const = 0;
    virtual void notifyPeers(const uint256& blockHash) const = 0;
};
#endif// I_PEER_BLOCK_NOTIFY_SERVICE_H