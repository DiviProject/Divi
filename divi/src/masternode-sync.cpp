// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include <masternode-sync.h>

#include <Logging.h>
#include <netfulfilledman.h>
#include <ui_interface.h>
#include <sync.h>

#include <MasternodeNetworkMessageManager.h>
#include <version.h>
#include <MasternodePaymentData.h>
#include <primitives/transaction.h>
#include <I_Clock.h>
#include <I_PeerSyncQueryService.h>
#include <I_BlockchainSyncQueryService.h>
#include <Node.h>
#include <NodeState.h>
// clang-format on

#include <algorithm>

static constexpr int64_t MASTERNODE_SYNC_TIMEOUT = 5;
static constexpr int64_t MASTERNODE_SYNC_THRESHOLD = 2;
const int CMasternodeSync::blockDepthUpToWhichToRequestMNWinners = 200;

static std::map<int,std::string> syncCodeNameByCodeValue =
    {
        {MasternodeSyncCode::MASTERNODE_SYNC_LIST,std::string("mnsync")},
        {MasternodeSyncCode::MASTERNODE_SYNC_MNW,std::string("mnwsync")}
    };

CMasternodeSync::CMasternodeSync(
    CNetFulfilledRequestManager& networkFulfilledRequestManager,
    const I_PeerSyncQueryService& peerSyncService,
    const I_Clock& clock,
    MasternodeNetworkMessageManager& networkMessageManager,
    MasternodePaymentData& masternodePaymentData
    ): networkFulfilledRequestManager_(networkFulfilledRequestManager)
    , peerSyncService_(peerSyncService)
    , clock_(clock)
    , networkMessageManager_(networkMessageManager)
    , masternodePaymentData_(masternodePaymentData)
    , timestampOfLastMasternodeListUpdate(0)
    , timestampOfLastMasternodeWinnerUpdate(0)
    , timestampOfLastFailedSync(0)
{
    Reset();
}

bool CMasternodeSync::IsSynced() const
{
    return currentMasternodeSyncStatus == MasternodeSyncCode::MASTERNODE_SYNC_FINISHED;
}

void CMasternodeSync::Reset()
{
    networkMessageManager_.mapSeenSyncMNB.clear();
    networkMessageManager_.mapSeenSyncMNW.clear();
    timestampOfLastMasternodeListUpdate = 0;
    timestampOfLastMasternodeWinnerUpdate = 0;
    countOfFailedSyncAttempts = 0;
    nominalNumberOfMasternodeBroadcastsReceived = 0;
    nominalNumberOfMasternodeWinnersReceived = 0;
    fulfilledMasternodeListSyncRequests = 0;
    fulfilledMasternodeWinnerSyncRequests = 0;
    currentMasternodeSyncStatus = MasternodeSyncCode::MASTERNODE_SYNC_INITIAL;
    totalSuccessivePeerSyncRequests = 0;
    lastSyncStageStartTimestamp = clock_.getTime();
}

void CMasternodeSync::SyncMasternodeWinnersWithPeer(CNode* node, int nCountNeeded)
{
    LOCK(masternodePaymentData_.cs_mapMasternodePayeeVotes);
    nCountNeeded = std::min(nCountNeeded,CMasternodeSync::blockDepthUpToWhichToRequestMNWinners);

    int nInvCount = 0;
    std::multimap<int, CMasternodePaymentWinner,std::greater<int>> winnersSortedByHeight;

    for(auto it = masternodePaymentData_.mapMasternodePayeeVotes.begin();
        it != masternodePaymentData_.mapMasternodePayeeVotes.end();
        ++it)
    {
        const CMasternodePaymentWinner& winner = (*it).second;
        winnersSortedByHeight.insert({winner.GetHeight(), winner});
    }

    const int startingHeight = (*winnersSortedByHeight.begin()).first;
    for(auto it = winnersSortedByHeight.begin();
        it != winnersSortedByHeight.end() &&
        (*it).first >= startingHeight-nCountNeeded;
        ++it)
    {
        const CMasternodePaymentWinner& winner = (*it).second;
        node->PushInventory(CInv(MSG_MASTERNODE_WINNER, winner.GetHash()));
        nInvCount++;
    }
    node->PushMessage("ssc", static_cast<int>(MasternodeSyncCode::MASTERNODE_SYNC_MNW), nInvCount);
}

void CMasternodeSync::DsegUpdate(CNode* pnode)
{
    if(networkMessageManager_.recordDsegUpdateAttempt(pnode->addr))
    {
        pnode->PushMessage("dseg", CTxIn());
    }
}

bool CMasternodeSync::HasRequestedMasternodeSyncTooOften(CNode* pfrom)
{
    bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());
    if (!isLocal)
    {
        if(networkMessageManager_.peerHasRequestedMasternodeListTooOften(pfrom->addr))
        {
            constexpr int penaltyForAskingTooManyTimes = 34;
            pfrom->GetNodeState()->ApplyMisbehavingPenalty(penaltyForAskingTooManyTimes,"Asked mn peer list too often");
            return true;
        }
    }
    return false;
}
bool CMasternodeSync::NotifyPeerOfMasternode(const CMasternode& mn, CNode* peer)
{
    if (!mn.addr.IsRFC1918() && mn.IsEnabled())
    {
        CMasternodeBroadcast mnb = CMasternodeBroadcast(mn);
        const uint256 hash = mnb.GetHash();
        peer->PushInventory(CInv(MSG_MASTERNODE_ANNOUNCE, hash));
        return true;
    }
    return false;
}
void CMasternodeSync::SyncMasternodeListWithPeer(CNode* peer)
{
    LOCK(networkMessageManager_.cs);
    int nInvCount = 0;
    for (const CMasternode& mn: networkMessageManager_.masternodes)
    {
        if (NotifyPeerOfMasternode(mn,peer))
        {
            LogPrint("masternode", "dseg - Sending Masternode entry - %s \n", mn.vin.prevout.hash);
            nInvCount++;
        }
    }
    peer->PushMessage("ssc", static_cast<int>(MasternodeSyncCode::MASTERNODE_SYNC_LIST), nInvCount);
    LogPrint("masternode", "dseg - Sent %d Masternode entries to peer %i\n", nInvCount, peer->GetId());
}
void CMasternodeSync::ProcessSyncUpdate(CNode* pfrom,const std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "mnget")
    { //Masternode Payments Request Sync

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (networkFulfilledRequestManager_.HasFulfilledRequest(pfrom->addr, "mnget"))
        {
            LogPrintf("%s : mnget - peer already asked me for the list\n", __func__);
            pfrom->GetNodeState()->ApplyMisbehavingPenalty(20,"Too recently asked for mn peer list");
            return;
        }

        networkFulfilledRequestManager_.AddFulfilledRequest(pfrom->addr, "mnget");
        SyncMasternodeWinnersWithPeer(pfrom, nCountNeeded);
        LogPrint("mnpayments", "mnget - Sent Masternode winners to peer %i\n", pfrom->GetId());
    }
    else if (strCommand == "dseg")
    { //Get Masternode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        bool peerIsRequestingMasternodeListSync = vin == CTxIn();
        if (peerIsRequestingMasternodeListSync && !HasRequestedMasternodeSyncTooOften(pfrom)) { //only should ask for this once
            SyncMasternodeListWithPeer(pfrom);
        }
        else if(!peerIsRequestingMasternodeListSync)
        {
            LOCK(networkMessageManager_.cs_process_message); // To avoid
            const CMasternode* pmn = networkMessageManager_.find(vin);
            if(pmn != nullptr && NotifyPeerOfMasternode(*pmn,pfrom) )
            {
                LogPrint("masternode", "dseg - Sent 1 Masternode entry to peer %i\n", pfrom->GetId());
            }
        }
    }
    else if (strCommand == "ssc")
    { //Sync status count
        int syncCode;
        int itemsSynced;
        vRecv >> syncCode >> itemsSynced;


        if (currentMasternodeSyncStatus >= MasternodeSyncCode::MASTERNODE_SYNC_FINISHED) return;
        if(syncCode != MasternodeSyncCode::MASTERNODE_SYNC_LIST && syncCode != MasternodeSyncCode::MASTERNODE_SYNC_MNW) return;
        if (syncCode != currentMasternodeSyncStatus) return;

        const std::string requestName = syncCodeNameByCodeValue[syncCode];
        if(networkFulfilledRequestManager_.HasFulfilledRequest(pfrom->addr,requestName)) return;
        if(!networkFulfilledRequestManager_.HasPendingRequest(pfrom->addr,requestName)) return;

        //this means we will receive no further communication
        switch (syncCode) {
        case (MasternodeSyncCode::MASTERNODE_SYNC_LIST):
            timestampOfLastMasternodeListUpdate = clock_.getTime();
            nominalNumberOfMasternodeBroadcastsReceived += itemsSynced;
            fulfilledMasternodeListSyncRequests++;
            break;
        case (MasternodeSyncCode::MASTERNODE_SYNC_MNW):
            timestampOfLastMasternodeWinnerUpdate = clock_.getTime();
            nominalNumberOfMasternodeWinnersReceived += itemsSynced;
            fulfilledMasternodeWinnerSyncRequests++;
            break;
        }

        networkFulfilledRequestManager_.FulfillPendingRequest(pfrom->addr,requestName);

        LogPrint("masternode", "%s - ssc - got inventory count %d %d\n",__func__, syncCode, itemsSynced);
    }
}

void CMasternodeSync::RecordMasternodeListUpdate(const uint256& hash)
{
    if(currentMasternodeSyncStatus != MasternodeSyncCode::MASTERNODE_SYNC_LIST) return;
    if (networkMessageManager_.broadcastIsKnown(hash)) {
        if (networkMessageManager_.mapSeenSyncMNB[hash] < MASTERNODE_SYNC_THRESHOLD) {
            timestampOfLastMasternodeListUpdate = clock_.getTime();
            networkMessageManager_.mapSeenSyncMNB[hash]++;
        }
    } else {
        timestampOfLastMasternodeListUpdate = clock_.getTime();
        networkMessageManager_.mapSeenSyncMNB.insert(std::make_pair(hash, 1));
    }
}

void CMasternodeSync::RecordMasternodeWinnerUpdate(const uint256& hash)
{
    if(currentMasternodeSyncStatus != MasternodeSyncCode::MASTERNODE_SYNC_MNW) return;
    if (masternodePaymentData_.winnerIsKnown(hash)) {
        if (networkMessageManager_.mapSeenSyncMNW[hash] < MASTERNODE_SYNC_THRESHOLD) {
            timestampOfLastMasternodeWinnerUpdate = clock_.getTime();
            networkMessageManager_.mapSeenSyncMNW[hash]++;
        }
    } else {
        timestampOfLastMasternodeWinnerUpdate = clock_.getTime();
        networkMessageManager_.mapSeenSyncMNW.insert(std::make_pair(hash, 1));
    }
}

void CMasternodeSync::ContinueToNextSyncStage()
{
    switch (currentMasternodeSyncStatus) {
    case (MasternodeSyncCode::MASTERNODE_SYNC_INITIAL):
    case (MasternodeSyncCode::MASTERNODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
        currentMasternodeSyncStatus = MasternodeSyncCode::MASTERNODE_SYNC_SPORKS;
        break;
    case (MasternodeSyncCode::MASTERNODE_SYNC_SPORKS):
        currentMasternodeSyncStatus = MasternodeSyncCode::MASTERNODE_SYNC_LIST;
        break;
    case (MasternodeSyncCode::MASTERNODE_SYNC_LIST):
        currentMasternodeSyncStatus = MasternodeSyncCode::MASTERNODE_SYNC_MNW;
        break;
    case (MasternodeSyncCode::MASTERNODE_SYNC_MNW):
        LogPrintf("CMasternodeSync::ContinueToNextSyncStage - Sync has finished\n");
        currentMasternodeSyncStatus = MasternodeSyncCode::MASTERNODE_SYNC_FINISHED;
        break;
    }
    totalSuccessivePeerSyncRequests = 0;
    lastSyncStageStartTimestamp = clock_.getTime();
}

std::string CMasternodeSync::GetSyncStatus()
{
    switch (currentMasternodeSyncStatus)
    {
    case MasternodeSyncCode::MASTERNODE_SYNC_INITIAL:
        return translate("Synchronization pending...");
    case MasternodeSyncCode::MASTERNODE_SYNC_SPORKS:
        return translate("Synchronizing sporks...");
    case MasternodeSyncCode::MASTERNODE_SYNC_LIST:
        return translate("Synchronizing masternodes...");
    case MasternodeSyncCode::MASTERNODE_SYNC_MNW:
        return translate("Synchronizing masternode winners...");
    case MasternodeSyncCode::MASTERNODE_SYNC_FAILED:
        return translate("Synchronization failed");
    case MasternodeSyncCode::MASTERNODE_SYNC_FINISHED:
        return translate("Synchronization finished");
    }
    return "";
}

bool CMasternodeSync::ShouldWaitForSync(const int64_t now)
{
    /* If the last processing time is in the future, it may mean that this is
       due to mocktime during testing.  But in any case it means something is
       weird, so let's start over.  */
    if (nTimeLastProcess > now) {
        LogPrint("masternode", "CMasternodeSync::Process() - WARNING: time went backwards, restarting sync...\n");
        Reset();
        nTimeLastProcess = now;
        return true;
    }

    if (now < nTimeLastProcess + MASTERNODE_SYNC_TIMEOUT) {
        return true;
    }

    // reset the sync process if the last call to this function was more than 60 minutes ago (client was in sleep mode)
    if(now - nTimeLastProcess > 60 * 60) {
        LogPrintf("CMasternodeSync::ProcessTick -- WARNING: no actions for too long, restarting sync...\n");
        Reset();
        nTimeLastProcess = now;
        return true;
    }
    nTimeLastProcess = now;

    //try syncing again
    if (currentMasternodeSyncStatus == MasternodeSyncCode::MASTERNODE_SYNC_FAILED &&
        timestampOfLastFailedSync > 0 && timestampOfLastFailedSync + (1 * 60) < now)
    {
        Reset();
    }
    else if (currentMasternodeSyncStatus == MasternodeSyncCode::MASTERNODE_SYNC_FAILED)
    {
        return true;
    }
    return false;
}

SyncStatus CMasternodeSync::SyncAssets(const CService& addr, const int64_t now, const int64_t lastUpdate, std::string assetType)
{
    LogPrint("masternode", "%s - %s %lld (GetTime() - MASTERNODE_SYNC_TIMEOUT) %lld\n",__func__,assetType, lastUpdate, now - MASTERNODE_SYNC_TIMEOUT);
    if (lastUpdate > 0 && lastUpdate < now - MASTERNODE_SYNC_TIMEOUT * 2 && totalSuccessivePeerSyncRequests >= MASTERNODE_SYNC_THRESHOLD)
    { //hasn't received a new item in the last five seconds, so we'll move to the
        ContinueToNextSyncStage();
        return SyncStatus::SYNC_STAGE_COMPLETE;
    }
    // timeout
    if (lastUpdate == 0 && now - lastSyncStageStartTimestamp > MASTERNODE_SYNC_TIMEOUT * 5)
    {
        LogPrintf("%s - ERROR - Sync has failed, will retry later (%s)\n",__func__,assetType);
        currentMasternodeSyncStatus = MasternodeSyncCode::MASTERNODE_SYNC_FAILED;
        totalSuccessivePeerSyncRequests = 0;
        timestampOfLastFailedSync = now;
        countOfFailedSyncAttempts++;
        return SyncStatus::FAIL;
    }

    if (networkFulfilledRequestManager_.HasFulfilledRequest(addr, assetType)) return SyncStatus::SUCCESS;
    if (networkFulfilledRequestManager_.HasPendingRequest(addr, assetType)) return SyncStatus::WAITING_FOR_SYNC;

    if (totalSuccessivePeerSyncRequests >= MASTERNODE_SYNC_THRESHOLD * 3) return SyncStatus::AT_PEER_SYNC_LIMIT;
    networkFulfilledRequestManager_.AddPendingRequest(addr, assetType);
    return SyncStatus::REQUEST_SYNC;
}
bool CMasternodeSync::SyncMasternodeList(CNode* pnode, const int64_t now)
{
    if (currentMasternodeSyncStatus == MasternodeSyncCode::MASTERNODE_SYNC_LIST)
    {
        const SyncStatus status = SyncAssets(pnode->addr,now,timestampOfLastMasternodeListUpdate,syncCodeNameByCodeValue[MasternodeSyncCode::MASTERNODE_SYNC_LIST]);
        switch(status)
        {
            case SyncStatus::FAIL: case SyncStatus::AT_PEER_SYNC_LIMIT:
            {
                return false;
            }
            case SyncStatus::SUCCESS: case SyncStatus::SYNC_STAGE_COMPLETE: case SyncStatus::WAITING_FOR_SYNC:
            {
                return true;
            }
            case SyncStatus::REQUEST_SYNC:
            {
                DsegUpdate(pnode);
                totalSuccessivePeerSyncRequests++;
                return false;
            }
            default:
            {
                break;
            }
        }
    }
    return true;
}

bool CMasternodeSync::IsMasternodeListSynced() const
{
    return currentMasternodeSyncStatus > MasternodeSyncCode::MASTERNODE_SYNC_LIST;
}
bool CMasternodeSync::SyncMasternodeWinners(CNode* pnode, const int64_t now)
{
    if (currentMasternodeSyncStatus == MasternodeSyncCode::MASTERNODE_SYNC_MNW)
    {
        const SyncStatus status = SyncAssets(pnode->addr,now,timestampOfLastMasternodeWinnerUpdate,syncCodeNameByCodeValue[MasternodeSyncCode::MASTERNODE_SYNC_MNW]);
        switch(status)
        {
            case SyncStatus::FAIL: case SyncStatus::AT_PEER_SYNC_LIMIT:
            {
                return false;
            }
            case SyncStatus::SUCCESS: case SyncStatus::SYNC_STAGE_COMPLETE: case SyncStatus::WAITING_FOR_SYNC:
            {
                return true;
            }
            case SyncStatus::REQUEST_SYNC:
            {
                pnode->PushMessage("mnget", CMasternodeSync::blockDepthUpToWhichToRequestMNWinners); //sync payees
                totalSuccessivePeerSyncRequests++;
                return false;
            }
            default:
            {
                break;
            }
        }
    }
    return true;
}
void CMasternodeSync::Process()
{
    const int64_t now = clock_.getTime();
    LogPrint("masternode", "Masternode sync process at %lld\n", now);

    if(ShouldWaitForSync(now)) return;

    LogPrint("masternode", "CMasternodeSync::Process() - currentMasternodeSyncStatus %d\n", currentMasternodeSyncStatus);

    if (currentMasternodeSyncStatus == MasternodeSyncCode::MASTERNODE_SYNC_INITIAL) ContinueToNextSyncStage();

    std::vector<NodeRef> vSporkSyncedNodes = peerSyncService_.GetSporkSyncedOrInboundNodes();

    // don't event attemp to sync if we don't have 3 synced nodes
    if(vSporkSyncedNodes.size() < 3) {
        return;
    }
    if (currentMasternodeSyncStatus == MasternodeSyncCode::MASTERNODE_SYNC_SPORKS)
    {
        ContinueToNextSyncStage();
    }

    for(NodeRef& pnode: vSporkSyncedNodes)
    {
        if (pnode->GetVersion() >=  ActiveProtocol() &&
            (!SyncMasternodeList(pnode.get(),now) || !SyncMasternodeWinners(pnode.get(),now)))
        {
            return;
        }
    }
}

void CMasternodeSync::AskForMN(CNode* pnode, const CTxIn& vin) const
{
    if(networkMessageManager_.recordMasternodeEntryRequestAttempt(vin.prevout))
    {
        pnode->PushMessage("dseg", vin);
    }
}

void CMasternodeSync::ClearTimedOutAndExpiredRequests(bool forceExpiredRemoval)
{
    networkMessageManager_.clearTimedOutAndExpiredRequests(forceExpiredRemoval);
}
