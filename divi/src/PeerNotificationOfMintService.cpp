#include <PeerNotificationOfMintService.h>
#include <uint256.h>
#include <Node.h>
#include <protocol.h>

PeerNotificationOfMintService::PeerNotificationOfMintService(
    std::vector<CNode*>& peers,
    CCriticalSection& peersLock
    ): peers_(peers)
    , peersLock_(peersLock)
{

}

bool PeerNotificationOfMintService::havePeersToNotify() const
{
    return !peers_.empty();
}
void PeerNotificationOfMintService::notifyPeers(const uint256& blockHash) const
{
    LOCK(peersLock_);
    for (CNode* peer : peers_)
    {
        peer->PushInventory(CInv(MSG_BLOCK, blockHash));
    }
}