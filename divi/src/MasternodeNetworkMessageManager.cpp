#include <MasternodeNetworkMessageManager.h>

#include <Logging.h>
#include <utiltime.h>

#define MASTERNODES_DSEG_SECONDS (3 * 60 * 60)
#define MASTERNODE_MIN_MNP_SECONDS (10 * 60)

MasternodeNetworkMessageManager::MasternodeNetworkMessageManager(
    ): masternodes()
    , mAskedUsForMasternodeList()
    , mWeAskedForMasternodeList()
    , mWeAskedForMasternodeListEntry()
    , nDsqCount(0)
    , mapSeenMasternodeBroadcast()
    , mapSeenMasternodePing()
{

}

void MasternodeNetworkMessageManager::clearTimedOutMasternodeListRequestsFromPeers()
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

void MasternodeNetworkMessageManager::clearTimedOutMasternodeListRequestsToPeers()
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
void MasternodeNetworkMessageManager::clearTimedOutMasternodeEntryRequests()
{
    std::map<COutPoint, int64_t>::iterator it2 = mWeAskedForMasternodeListEntry.begin();
    while (it2 != mWeAskedForMasternodeListEntry.end()) {
        if ((*it2).second < GetTime()) {
            mWeAskedForMasternodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }
}
void MasternodeNetworkMessageManager::clearExpiredMasternodeEntryRequests(const COutPoint& masternodeCollateral)
{
    // allow us to ask for this masternode again if we see another ping
    std::map<COutPoint, int64_t>::iterator it2 = mWeAskedForMasternodeListEntry.find(masternodeCollateral);
    if(it2 != mWeAskedForMasternodeListEntry.end())
    {
        mWeAskedForMasternodeListEntry.erase(it2);
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

bool MasternodeNetworkMessageManager::recordMasternodeEntryRequestAttempt(const COutPoint& masternodeCollateral)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForMasternodeListEntry.find(masternodeCollateral);
    if (i != mWeAskedForMasternodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t) return false; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrint("masternode", "CMasternodeMan::AskForMN - Asking node for missing entry, vin: %s\n", masternodeCollateral.hash.ToString());
    int64_t askAgain = GetTime() + MASTERNODE_MIN_MNP_SECONDS;
    mWeAskedForMasternodeListEntry[masternodeCollateral] = askAgain;

    return true;
}

void MasternodeNetworkMessageManager::Clear()
{
    mAskedUsForMasternodeList.clear();
    mWeAskedForMasternodeList.clear();
    mWeAskedForMasternodeListEntry.clear();
}

std::string MasternodeNetworkMessageManager::ToString() const
{
    std::ostringstream info;

    info << "peers who asked us for Masternode list: "
        << (int)mAskedUsForMasternodeList.size()
        << ", peers we asked for Masternode list: "
        << (int)mWeAskedForMasternodeList.size()
        << ", entries in Masternode list we asked for: "
        << (int)mWeAskedForMasternodeListEntry.size()
        << ", nDsqCount: "
        << (int)nDsqCount;

    return info.str();
}