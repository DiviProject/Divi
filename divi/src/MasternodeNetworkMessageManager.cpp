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

void MasternodeNetworkMessageManager::clearExpiredMasternodeListRequestsToPeers()
{
    // check who we asked for the Masternode list
    auto it1 = mWeAskedForMasternodeList.begin();
    while (it1 != mWeAskedForMasternodeList.end()) {
        if ((*it1).second < GetTime()) {
            mWeAskedForMasternodeList.erase(it1++);
        } else {
            ++it1;
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

bool MasternodeNetworkMessageManager::recordDsegUpdateAttempt(const CAddress& peerAddress)
{
    if (!(peerAddress.IsRFC1918() || peerAddress.IsLocal())) {
        std::map<CNetAddr, int64_t>::iterator it = mWeAskedForMasternodeList.find(peerAddress);
        if (it != mWeAskedForMasternodeList.end()) {
            if (GetTime() < (*it).second) {
                LogPrint("masternode", "dseg - we already asked peer %s for the list; skipping...\n", peerAddress.ToString());
                return false;
            }
        }
    }

    int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
    mWeAskedForMasternodeList[peerAddress] = askAgain;
    return true;
}

void MasternodeNetworkMessageManager::clear()
{
    mAskedUsForMasternodeList.clear();
    mWeAskedForMasternodeList.clear();
}

std::string MasternodeNetworkMessageManager::ToString() const
{
    std::ostringstream info;

    info << "peers who asked us for Masternode list: "
        << (int)mAskedUsForMasternodeList.size()
        << ", peers we asked for Masternode list: "
        << (int)mWeAskedForMasternodeList.size();

    return info.str();
}