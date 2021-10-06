#include <MasternodeNetworkMessageManager.h>

#include <Logging.h>
#include <utiltime.h>
#include <masternode-sync.h>
#include <version.h>

#define MASTERNODES_DSEG_SECONDS (3 * 60 * 60)
#define MASTERNODE_MIN_MNP_SECONDS (10 * 60)

MasternodeNetworkMessageManager::MasternodeNetworkMessageManager(
    ): nDsqCount(0)
    , mAskedUsForMasternodeList()
    , mWeAskedForMasternodeList()
    , mWeAskedForMasternodeListEntry()
    , mapSeenMasternodeBroadcast()
    , mapSeenMasternodePing()
    , cs()
    , cs_process_message()
    , masternodes()
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

void MasternodeNetworkMessageManager::clearTimedOutAndExpiredRequests(bool forceExpiredRemoval)
{
   LOCK2(cs_process_message, cs);

    //remove inactive and outdated
    std::vector<CMasternode>::iterator it = masternodes.begin();
    while (it != masternodes.end())
    {
        if ((*it).activeState == CMasternode::MASTERNODE_REMOVE ||
            (*it).activeState == CMasternode::MASTERNODE_VIN_SPENT ||
            (forceExpiredRemoval && (*it).activeState == CMasternode::MASTERNODE_EXPIRED) ||
            (*it).protocolVersion < ActiveProtocol())
        {
            LogPrint("masternode", "CMasternodeMan: Removing inactive Masternode %s - %i now\n", (*it).vin.prevout.hash, masternodeCount() - 1);

            clearExpiredMasternodeBroadcasts(it->vin.prevout);
            clearExpiredMasternodeEntryRequests(it->vin.prevout);
            it = masternodes.erase(it);
        } else {
            ++it;
        }
    }

    clearTimedOutMasternodeListRequestsFromPeers();
    clearTimedOutMasternodeListRequestsToPeers();
    clearTimedOutMasternodeEntryRequests();
    clearTimedOutMasternodeBroadcasts();
    clearTimedOutMasternodePings();
}

void MasternodeNetworkMessageManager::clearTimedOutMasternodeListRequestsFromPeers()
{
    AssertLockHeld(cs_process_message);
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
    AssertLockHeld(cs_process_message);
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
    AssertLockHeld(cs_process_message);
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
    AssertLockHeld(cs_process_message);
    std::map<uint256, CMasternodePing>::iterator it4 = mapSeenMasternodePing.begin();
    while (it4 != mapSeenMasternodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (MASTERNODE_REMOVAL_SECONDS * 2)) {
            mapSeenMasternodePing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

void MasternodeNetworkMessageManager::clearTimedOutMasternodeBroadcasts()
{
    AssertLockHeld(cs_process_message);
    std::map<uint256, CMasternodeBroadcast>::iterator it3 = mapSeenMasternodeBroadcast.begin();
    while (it3 != mapSeenMasternodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (MASTERNODE_REMOVAL_SECONDS * 2)) {
            mapSeenMasternodeBroadcast.erase(it3++);
            mapSeenSyncMNB.erase((*it3).second.GetHash());
        } else {
            ++it3;
        }
    }
}
void MasternodeNetworkMessageManager::clearExpiredMasternodeBroadcasts(const COutPoint& collateral)
{
    AssertLockHeld(cs_process_message);
    std::map<uint256, CMasternodeBroadcast>::iterator it3 = mapSeenMasternodeBroadcast.begin();
    while (it3 != mapSeenMasternodeBroadcast.end()) {
        if ((*it3).second.vin.prevout == collateral) {
            mapSeenSyncMNB.erase((*it3).first);
            mapSeenMasternodeBroadcast.erase(it3++);
        } else {
            ++it3;
        }
    }
}

void MasternodeNetworkMessageManager::clearExpiredMasternodeEntryRequests(const COutPoint& masternodeCollateral)
{
    AssertLockHeld(cs_process_message);
    // allow us to ask for this masternode again if we see another ping
    std::map<COutPoint, int64_t>::iterator it2 = mWeAskedForMasternodeListEntry.find(masternodeCollateral);
    if(it2 != mWeAskedForMasternodeListEntry.end())
    {
        mWeAskedForMasternodeListEntry.erase(it2);
    }
}

bool MasternodeNetworkMessageManager::peerHasRequestedMasternodeListTooOften(const CAddress& peerAddress)
{
    LOCK(cs_process_message);
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
    LOCK(cs_process_message);
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
    LOCK(cs_process_message);
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
    LOCK2(cs_process_message,cs);
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
    LOCK(cs_process_message);
    return mapSeenMasternodeBroadcast.count(broadcastHash) >0;
}
bool MasternodeNetworkMessageManager::pingIsKnown(const uint256& pingHash) const
{
    LOCK(cs_process_message);
    return mapSeenMasternodePing.count(pingHash) >0;
}
void MasternodeNetworkMessageManager::recordPing(const CMasternodePing& mnp)
{
    LOCK2(cs_process_message, cs);
    const CMasternode* pmn = nullptr;
    for(const CMasternode& mn: masternodes)
    {
        if (mn.vin.prevout == mnp.vin.prevout)
        {
            pmn = &mn;
            break;
        }
    }

    mapSeenMasternodePing[mnp.GetHash()] = mnp;
    if (pmn != nullptr)
    {
        const uint256 mnbHash = CMasternodeBroadcast(*pmn).GetHash();
        auto mit = mapSeenMasternodeBroadcast.find(mnbHash);
        if (mit != mapSeenMasternodeBroadcast.end())
        {
            mit->second.lastPing = mnp;
        }
    }
}
void MasternodeNetworkMessageManager::recordBroadcast(const CMasternodeBroadcast& mnb)
{
    LOCK(cs_process_message);
    const uint256 hash = mnb.GetHash();
    if(broadcastIsKnown(hash)) return;
    mapSeenMasternodeBroadcast.emplace(hash,mnb);
}
const CMasternodeBroadcast& MasternodeNetworkMessageManager::getKnownBroadcast(const uint256& broadcastHash) const
{
    LOCK(cs_process_message);
    static CMasternodeBroadcast dummyBroadcast;
    std::map<uint256, CMasternodeBroadcast>::const_iterator it = mapSeenMasternodeBroadcast.find(broadcastHash);
    if(it != mapSeenMasternodeBroadcast.end())
    {
        return mapSeenMasternodeBroadcast.find(broadcastHash)->second;
    }
    else
    {
        return dummyBroadcast;
    }
}
const CMasternodePing& MasternodeNetworkMessageManager::getKnownPing(const uint256& pingHash) const
{
    LOCK(cs_process_message);
    static CMasternodePing dummyPing;
    std::map<uint256, CMasternodePing>::const_iterator it = mapSeenMasternodePing.find(pingHash);
    if(it != mapSeenMasternodePing.end())
    {
        return mapSeenMasternodePing.find(pingHash)->second;
    }
    else
    {
        return dummyPing;
    }
}
const CMasternode* MasternodeNetworkMessageManager::find(const CTxIn& vin) const
{
    LOCK(cs);

    for(const CMasternode& mn: masternodes)
    {
        if (mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return nullptr;
}