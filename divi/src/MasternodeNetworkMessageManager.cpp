#include <MasternodeNetworkMessageManager.h>


MasternodeNetworkMessageManager::MasternodeNetworkMessageManager(
    ): mAskedUsForMasternodeList()
    , mWeAskedForMasternodeList()
    , mWeAskedForMasternodeListEntry()
    , nDsqCount(0)
    , mapSeenMasternodeBroadcast()
    , mapSeenMasternodePing()
{

}