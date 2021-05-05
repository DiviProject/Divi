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
// clang-format on

#include <algorithm>

static constexpr int64_t MASTERNODE_SYNC_TIMEOUT = 5;
static constexpr int64_t MASTERNODE_SYNC_THRESHOLD = 2;

CMasternodeSync::CMasternodeSync(
    CNetFulfilledRequestManager& networkFulfilledRequestManager,
    const I_PeerSyncQueryService& peerSyncService,
    const I_Clock& clock,
    const I_BlockchainSyncQueryService& blockchainSync,
    MasternodeNetworkMessageManager& networkMessageManager,
    MasternodePaymentData& masternodePaymentData
    ): networkFulfilledRequestManager_(networkFulfilledRequestManager)
    , peerSyncService_(peerSyncService)
    , clock_(clock)
    , blockchainSync_(blockchainSync)
    , networkMessageManager_(networkMessageManager)
    , masternodePaymentData_(masternodePaymentData)
{
    Reset();
}

bool CMasternodeSync::IsSynced() const
{
    return currentMasternodeSyncStatus == MasternodeSyncCode::MASTERNODE_SYNC_FINISHED;
}

void CMasternodeSync::Reset()
{
    waitingForBlockchainSync = false;
    timestampOfLastMasternodeListUpdate = 0;
    timestampOfLastMasternodeWinnerUpdate = 0;
    mapSeenSyncMNB.clear();
    mapSeenSyncMNW.clear();
    countOfFailedSyncAttempts = 0;
    nominalNumberOfMasternodeBroadcastsReceived = 0;
    nominalNumberOfMasternodeWinnersReceived = 0;
    fulfilledMasternodeListSyncRequests = 0;
    fulfilledMasternodeWinnerSyncRequests = 0;
    currentMasternodeSyncStatus = MasternodeSyncCode::MASTERNODE_SYNC_INITIAL;
    totalSuccessivePeerSyncRequests = 0;
    lastSyncStageStartTimestamp = clock_.getTime();
}

void CMasternodeSync::DsegUpdate(CNode* pnode)
{
    LOCK(networkMessageManager_.cs);

    if(networkMessageManager_.recordDsegUpdateAttempt(pnode->addr))
    {
        pnode->PushMessage("dseg", CTxIn());
    }
}

void CMasternodeSync::AddedMasternodeList(const uint256& hash)
{
    if (networkMessageManager_.broadcastIsKnown(hash)) {
        if (mapSeenSyncMNB[hash] < MASTERNODE_SYNC_THRESHOLD) {
            timestampOfLastMasternodeListUpdate = clock_.getTime();
            mapSeenSyncMNB[hash]++;
        }
    } else {
        timestampOfLastMasternodeListUpdate = clock_.getTime();
        mapSeenSyncMNB.insert(std::make_pair(hash, 1));
    }
}

void CMasternodeSync::AddedMasternodeWinner(const uint256& hash)
{
    if (masternodePaymentData_.masternodeWinnerVoteIsKnown(hash)) {
        if (mapSeenSyncMNW[hash] < MASTERNODE_SYNC_THRESHOLD) {
            timestampOfLastMasternodeWinnerUpdate = clock_.getTime();
            mapSeenSyncMNW[hash]++;
        }
    } else {
        timestampOfLastMasternodeWinnerUpdate = clock_.getTime();
        mapSeenSyncMNW.insert(std::make_pair(hash, 1));
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

void CMasternodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "ssc") { //Sync status count
        int syncCode;
        int itemsSynced;
        vRecv >> syncCode >> itemsSynced;


        if (currentMasternodeSyncStatus >= MasternodeSyncCode::MASTERNODE_SYNC_FINISHED) return;

        //this means we will receive no further communication
        switch (syncCode) {
        case (MasternodeSyncCode::MASTERNODE_SYNC_LIST):
            if (syncCode != currentMasternodeSyncStatus) return;
            if(itemsSynced == 0) {
                timestampOfLastMasternodeListUpdate = clock_.getTime();
            }
            nominalNumberOfMasternodeBroadcastsReceived += itemsSynced;
            fulfilledMasternodeListSyncRequests++;
            break;
        case (MasternodeSyncCode::MASTERNODE_SYNC_MNW):
            if (syncCode != currentMasternodeSyncStatus) return;
            if(itemsSynced == 0) {
                timestampOfLastMasternodeWinnerUpdate = clock_.getTime();
            }
            nominalNumberOfMasternodeWinnersReceived += itemsSynced;
            fulfilledMasternodeWinnerSyncRequests++;
            break;
        }

        LogPrint("masternode", "CMasternodeSync:ProcessMessage - ssc - got inventory count %d %d\n", syncCode, itemsSynced);
    }
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
    if (currentMasternodeSyncStatus == MasternodeSyncCode::MASTERNODE_SYNC_FAILED && timestampOfLastFailedSync + (1 * 60) < now) {
        Reset();
    } else if (currentMasternodeSyncStatus == MasternodeSyncCode::MASTERNODE_SYNC_FAILED) {
        return true;
    }
    return false;
}

SyncStatus CMasternodeSync::SyncAssets(CNode* pnode, const int64_t now, const int64_t lastUpdate, std::string assetType)
{
    LogPrint("masternode", "%s - %s %lld (GetTime() - MASTERNODE_SYNC_TIMEOUT) %lld\n",__func__,assetType, lastUpdate, now - MASTERNODE_SYNC_TIMEOUT);
    if (lastUpdate > 0 && lastUpdate < now - MASTERNODE_SYNC_TIMEOUT * 2 && totalSuccessivePeerSyncRequests >= MASTERNODE_SYNC_THRESHOLD)
    { //hasn't received a new item in the last five seconds, so we'll move to the
        ContinueToNextSyncStage();
        return SyncStatus::FAIL;
    }

    if (networkFulfilledRequestManager_.HasFulfilledRequest(pnode->addr, assetType)) return SyncStatus::SUCCESS;
    networkFulfilledRequestManager_.AddFulfilledRequest(pnode->addr, assetType);

    // timeout
    if (lastUpdate == 0 &&
            (totalSuccessivePeerSyncRequests >= MASTERNODE_SYNC_THRESHOLD * 3 || now - lastSyncStageStartTimestamp > MASTERNODE_SYNC_TIMEOUT * 5))
    {
        LogPrintf("%s - ERROR - Sync has failed, will retry later (%s)\n",__func__,assetType);
        currentMasternodeSyncStatus = MasternodeSyncCode::MASTERNODE_SYNC_FAILED;
        totalSuccessivePeerSyncRequests = 0;
        timestampOfLastFailedSync = now;
        countOfFailedSyncAttempts++;
        return SyncStatus::FAIL;
    }

    if (totalSuccessivePeerSyncRequests >= MASTERNODE_SYNC_THRESHOLD * 3) return SyncStatus::FAIL;
    return SyncStatus::REQUEST_SYNC;
}
bool CMasternodeSync::MasternodeListIsSynced(CNode* pnode, const int64_t now)
{
    if (currentMasternodeSyncStatus == MasternodeSyncCode::MASTERNODE_SYNC_LIST)
    {
        const SyncStatus status = SyncAssets(pnode,now,timestampOfLastMasternodeListUpdate,"mnsync");
        switch(status)
        {
            case SyncStatus::FAIL:
            {
                return false;
            }
            case SyncStatus::SUCCESS:
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


        return false;
    }
    return true;
}
uint32_t CMasternodeSync::masternodeCount() const
{
    return networkMessageManager_.masternodeCount();
}

bool CMasternodeSync::IsMasternodeListSynced() const
{
    return currentMasternodeSyncStatus > MasternodeSyncCode::MASTERNODE_SYNC_LIST;
}
bool CMasternodeSync::MasternodeWinnersListIsSync(CNode* pnode, const int64_t now)
{
    if (currentMasternodeSyncStatus == MasternodeSyncCode::MASTERNODE_SYNC_MNW)
    {
        const SyncStatus status = SyncAssets(pnode,now,timestampOfLastMasternodeWinnerUpdate,"mnwsync");
        switch(status)
        {
            case SyncStatus::FAIL:
            {
                return false;
            }
            case SyncStatus::SUCCESS:
            {
                return true;
            }
            case SyncStatus::REQUEST_SYNC:
            {
                int nMnCount = masternodeCount();
                pnode->PushMessage("mnget", nMnCount); //sync payees
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
void CMasternodeSync::Process(bool networkIsRegtest)
{
    const int64_t now = clock_.getTime();
    LogPrint("masternode", "Masternode sync process at %lld\n", now);

    if(ShouldWaitForSync(now)) return;

    LogPrint("masternode", "CMasternodeSync::Process() - currentMasternodeSyncStatus %d\n", currentMasternodeSyncStatus);

    if (currentMasternodeSyncStatus == MasternodeSyncCode::MASTERNODE_SYNC_INITIAL) ContinueToNextSyncStage();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if (!networkIsRegtest && !blockchainSync_.isBlockchainSynced() && currentMasternodeSyncStatus > MasternodeSyncCode::MASTERNODE_SYNC_SPORKS)
    {
        waitingForBlockchainSync = true;
        return;
    }

    /* If we were waiting for the blockchain to catch up and that just finished,
       reset the time for when we started syncing the current asset (which will
       be masternode list).  Otherwise it may have timed out while we were
       waiting for the blockchain sync.  */
    if (waitingForBlockchainSync)
    {
        if (currentMasternodeSyncStatus != MASTERNODE_SYNC_LIST)
        {
            /* This should not normally happen, since we set
               waitingForBlockchainSync to true when we reach the item
               after sporks (which is the masternode list).

               It could happen in edge cases, though, for instance if
               IsBlockchainSynced was true but was reset to false due to
               a laptop sleeping, or similar situations.  */
            LogPrint("masternode", "%s - unexpected masternode sync status, resetting");
            Reset ();
            return;
        }

        lastSyncStageStartTimestamp = clock_.getTime();
        waitingForBlockchainSync = false;
        LogPrint("masternode", "%s - blockchain sync is finished, restarting mn list sync\n", __func__);
    }

    std::vector<CNode*> vSporkSyncedNodes = peerSyncService_.GetSporkSyncedOrInboundNodes();

    // don't event attemp to sync if we don't have 3 synced nodes
    if(vSporkSyncedNodes.size() < 3) {
        return;
    }

    for(CNode* pnode: vSporkSyncedNodes)
    {
        //set to synced
        if (currentMasternodeSyncStatus == MasternodeSyncCode::MASTERNODE_SYNC_SPORKS)
        {
            // this has to be safe to do, because we will get here only if we have 3 peers
            if (totalSuccessivePeerSyncRequests >= 2) ContinueToNextSyncStage();
            totalSuccessivePeerSyncRequests++;
            return;
        }

        if (pnode->nVersion >=  ActiveProtocol() &&
            (!MasternodeListIsSynced(pnode,now) || !MasternodeWinnersListIsSync(pnode,now)))
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
