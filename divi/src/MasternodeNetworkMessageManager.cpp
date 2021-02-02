#include <MasternodeNetworkMessageManager.h>


MasternodeNetworkMessageManager::MasternodeNetworkMessageManager(
    ): mAskedUsForMasternodeList()
    , mWeAskedForMasternodeList()
    , mWeAskedForMasternodeListEntry()
    , mapSeenMasternodeBroadcast()
    , mapSeenMasternodePing()
{

}