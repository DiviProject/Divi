#include <MasternodeNetworkMessageManager.h>

#include <Logging.h>
#include <utiltime.h>

MasternodeNetworkMessageManager::MasternodeNetworkMessageManager(
    ): mAskedUsForMasternodeList()
    , mWeAskedForMasternodeList()
    , mWeAskedForMasternodeListEntry()
    , nDsqCount(0)
    , mapSeenMasternodeBroadcast()
    , mapSeenMasternodePing()
{

}

void MasternodeNetworkMessageManager::clearExpiredMasternodeListRequestsFromPeers()
{
    // check who's asked for the Masternode list
    std::map<CNetAddr, int64_t>::iterator it = mAskedUsForMasternodeList.begin();
    while (it != mAskedUsForMasternodeList.end()) {
        if ((*it).second < GetTime()) {
            mAskedUsForMasternodeList.erase(it++);
        } else {
            ++it;
        }
    }
}
bool MasternodeNetworkMessageManager::peerHasRequestedMasternodeListTooOften(const CAddress& peerAddress)
{
    std::map<CNetAddr, int64_t>::iterator it = mAskedUsForMasternodeList.find(peerAddress);
    if (it != mAskedUsForMasternodeList.end()) {
        int64_t t = (*it).second;
        if (GetTime() < t) {
            LogPrintf("%s : dseg - peer already asked me for the list\n", __func__);
            return true;
        }
    }
    int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
    mAskedUsForMasternodeList[peerAddress] = askAgain;
    return false;
}
void MasternodeNetworkMessageManager::clear()
{
    mAskedUsForMasternodeList.clear();
}

std::string MasternodeNetworkMessageManager::ToString() const
{
    std::ostringstream info;

    info << "peers who asked us for Masternode list: "
        << (int)mAskedUsForMasternodeList.size();

    return info.str();
}