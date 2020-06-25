#include <PeerNotificationOfMintService.h>
#include <uint256.h>
#include <net.h>
#include <protocol.h>

PeerNotificationOfMintService::PeerNotificationOfMintService(
    std::vector<CNode*>& peers
    ): peers_(peers)
{

}

bool PeerNotificationOfMintService::havePeersToNotify() const
{
    return !peers_.empty();
}
void PeerNotificationOfMintService::notifyPeers(const uint256& blockHash) const
{
    for (CNode* peer : peers_) 
    {
        peer->PushInventory(CInv(MSG_BLOCK, blockHash));
    }
}