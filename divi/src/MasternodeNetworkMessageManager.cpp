#include <MasternodeNetworkMessageManager.h>

#include <Logging.h>
#include <utiltime.h>
#include <masternode-sync.h>

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

uint32_t MasternodeNetworkMessageManager::masternodeCount() const
{
    return masternodes.size();
}

const std::vector<CMasternode>& MasternodeNetworkMessageManager::GetFullMasternodeVector() const
{
    LOCK(cs);
    return masternodes;
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

void MasternodeNetworkMessageManager::clearTimedOutMasternodePings()
{
    std::map<uint256, CMasternodePing>::iterator it4 = mapSeenMasternodePing.begin();
    while (it4 != mapSeenMasternodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (MASTERNODE_REMOVAL_SECONDS * 2)) {
            mapSeenMasternodePing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

void MasternodeNetworkMessageManager::clearTimedOutMasternodeBroadcasts(CMasternodeSync& masternodeSynchronization)
{
    std::map<uint256, CMasternodeBroadcast>::iterator it3 = mapSeenMasternodeBroadcast.begin();
    while (it3 != mapSeenMasternodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (MASTERNODE_REMOVAL_SECONDS * 2)) {
            mapSeenMasternodeBroadcast.erase(it3++);
            masternodeSynchronization.mapSeenSyncMNB.erase((*it3).second.GetHash());
        } else {
            ++it3;
        }
    }
}
void MasternodeNetworkMessageManager::clearExpiredMasternodeBroadcasts(const COutPoint& collateral, CMasternodeSync& masternodeSynchronization)
{
    std::map<uint256, CMasternodeBroadcast>::iterator it3 = mapSeenMasternodeBroadcast.begin();
    while (it3 != mapSeenMasternodeBroadcast.end()) {
        if ((*it3).second.vin.prevout == collateral) {
            masternodeSynchronization.mapSeenSyncMNB.erase((*it3).first);
            mapSeenMasternodeBroadcast.erase(it3++);
        } else {
            ++it3;
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
                LogPrint("masternode", "dseg - we already asked peer %s for the list; skipping...\n", peerAddress);
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

    LogPrint("masternode", "%s - Asking node for missing entry, vin: %s\n", __func__, masternodeCollateral.hash);
    int64_t askAgain = GetTime() + MASTERNODE_MIN_MNP_SECONDS;
    mWeAskedForMasternodeListEntry[masternodeCollateral] = askAgain;

    return true;
}

void MasternodeNetworkMessageManager::Clear()
{
    LOCK(cs);
    masternodes.clear();
    mAskedUsForMasternodeList.clear();
    mWeAskedForMasternodeList.clear();
    mWeAskedForMasternodeListEntry.clear();
    mapSeenMasternodeBroadcast.clear();
    mapSeenMasternodePing.clear();
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

bool MasternodeNetworkMessageManager::broadcastIsKnown(const uint256& broadcastHash) const
{
    return mapSeenMasternodeBroadcast.count(broadcastHash) >0;
}
bool MasternodeNetworkMessageManager::pingIsKnown(const uint256& pingHash) const
{
    return mapSeenMasternodePing.count(pingHash) >0;
}
const CMasternodeBroadcast& MasternodeNetworkMessageManager::getKnownBroadcast(const uint256& broadcastHash)
{
    return mapSeenMasternodeBroadcast[broadcastHash];
}
const CMasternodePing& MasternodeNetworkMessageManager::getKnownPing(const uint256& pingHash)
{
    return mapSeenMasternodePing[pingHash];
}
