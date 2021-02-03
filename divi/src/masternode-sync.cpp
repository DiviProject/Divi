// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "masternode-sync.h"
#include "masternode-payments.h"
#include "masternode.h"
#include "masternodeman.h"
#include "spork.h"
#include "utiltime.h"
#include "Logging.h"
#include "addrman.h"
#include "netfulfilledman.h"
#include <ui_interface.h>
#include <sync.h>
#include <string>
#include <chain.h>
// clang-format on

#include <algorithm>

static constexpr int64_t MASTERNODE_SYNC_TIMEOUT = 5;
static constexpr int64_t MASTERNODE_SYNC_THRESHOLD = 2;
extern CCriticalSection cs_main;
extern bool fImporting;
extern bool fReindex;
extern CChain chainActive;


class CMasternodeSync;
CMasternodeSync masternodeSync;

CMasternodeSync::CMasternodeSync()
{
    Reset();
}

bool CMasternodeSync::IsSynced() const
{
    return RequestedMasternodeAssets == MASTERNODE_SYNC_FINISHED;
}

bool CMasternodeSync::IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    static int64_t lastProcess = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    const int64_t now = GetTime();
    if (now - lastProcess > 60 * 60) {
        Reset();
        fBlockchainSynced = false;
    }
    lastProcess = now;

    if (fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain) return false;

    CBlockIndex* pindex = chainActive.Tip();
    if (pindex == NULL) return false;


    if (pindex->nTime + 60 * 6000 < now)
        return false;

    fBlockchainSynced = true;

    return true;
}

void CMasternodeSync::Reset()
{
    lastMasternodeList = 0;
    lastMasternodeWinner = 0;
    mapSeenSyncMNB.clear();
    mapSeenSyncMNW.clear();
    nCountFailures = 0;
    sumMasternodeList = 0;
    sumMasternodeWinner = 0;
    countMasternodeList = 0;
    countMasternodeWinner = 0;
    RequestedMasternodeAssets = MASTERNODE_SYNC_INITIAL;
    RequestedMasternodeAttempt = 0;
    nAssetSyncStarted = GetTime();
    mnodeman.ResetRankingCache();
}

void CMasternodeSync::AddedMasternodeList(const uint256& hash)
{
    if (mnodeman.mapSeenMasternodeBroadcast.count(hash)) {
        if (mapSeenSyncMNB[hash] < MASTERNODE_SYNC_THRESHOLD) {
            lastMasternodeList = GetTime();
            mapSeenSyncMNB[hash]++;
        }
    } else {
        lastMasternodeList = GetTime();
        mapSeenSyncMNB.insert(std::make_pair(hash, 1));
    }
}

void CMasternodeSync::AddedMasternodeWinner(const uint256& hash)
{
    if (masternodePayments.GetPaymentWinnerForHash(hash) != nullptr) {
        if (mapSeenSyncMNW[hash] < MASTERNODE_SYNC_THRESHOLD) {
            lastMasternodeWinner = GetTime();
            mapSeenSyncMNW[hash]++;
        }
    } else {
        lastMasternodeWinner = GetTime();
        mapSeenSyncMNW.insert(std::make_pair(hash, 1));
    }
}

void CMasternodeSync::GetNextAsset()
{
    switch (RequestedMasternodeAssets) {
    case (MASTERNODE_SYNC_INITIAL):
    case (MASTERNODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
        RequestedMasternodeAssets = MASTERNODE_SYNC_SPORKS;
        break;
    case (MASTERNODE_SYNC_SPORKS):
        RequestedMasternodeAssets = MASTERNODE_SYNC_LIST;
        break;
    case (MASTERNODE_SYNC_LIST):
        RequestedMasternodeAssets = MASTERNODE_SYNC_MNW;
        break;
    case (MASTERNODE_SYNC_MNW):
        LogPrintf("CMasternodeSync::GetNextAsset - Sync has finished\n");
        RequestedMasternodeAssets = MASTERNODE_SYNC_FINISHED;
        break;
    }
    RequestedMasternodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CMasternodeSync::GetSyncStatus()
{
    switch (RequestedMasternodeAssets)
    {
    case MASTERNODE_SYNC_INITIAL:
        return translate("Synchronization pending...");
    case MASTERNODE_SYNC_SPORKS:
        return translate("Synchronizing sporks...");
    case MASTERNODE_SYNC_LIST:
        return translate("Synchronizing masternodes...");
    case MASTERNODE_SYNC_MNW:
        return translate("Synchronizing masternode winners...");
    case MASTERNODE_SYNC_FAILED:
        return translate("Synchronization failed");
    case MASTERNODE_SYNC_FINISHED:
        return translate("Synchronization finished");
    }
    return "";
}

void CMasternodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "ssc") { //Sync status count
        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;


        if (RequestedMasternodeAssets >= MASTERNODE_SYNC_FINISHED) return;

        //this means we will receive no further communication
        switch (nItemID) {
        case (MASTERNODE_SYNC_LIST):
            if (nItemID != RequestedMasternodeAssets) return;
            if(nCount == 0) {
                lastMasternodeList = GetTime();
            }
            sumMasternodeList += nCount;
            countMasternodeList++;
            break;
        case (MASTERNODE_SYNC_MNW):
            if (nItemID != RequestedMasternodeAssets) return;
            if(nCount == 0) {
                lastMasternodeWinner = GetTime();
            }
            sumMasternodeWinner += nCount;
            countMasternodeWinner++;
            break;
        }

        LogPrint("masternode", "CMasternodeSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
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
    if (RequestedMasternodeAssets == MASTERNODE_SYNC_FAILED && lastFailure + (1 * 60) < now) {
        Reset();
    } else if (RequestedMasternodeAssets == MASTERNODE_SYNC_FAILED) {
        return true;
    }
    return false;
}

SyncStatus CMasternodeSync::SyncAssets(CNode* pnode, const int64_t now, const int64_t lastUpdate, std::string assetType)
{
    LogPrint("masternode", "%s - %s %lld (GetTime() - MASTERNODE_SYNC_TIMEOUT) %lld\n",__func__,assetType, lastUpdate, now - MASTERNODE_SYNC_TIMEOUT);
    if (lastUpdate > 0 && lastUpdate < now - MASTERNODE_SYNC_TIMEOUT * 2 && RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD)
    { //hasn't received a new item in the last five seconds, so we'll move to the
        GetNextAsset();
        return SyncStatus::FAIL;
    }

    if (netfulfilledman.HasFulfilledRequest(pnode->addr, assetType)) return SyncStatus::SUCCESS;
    netfulfilledman.AddFulfilledRequest(pnode->addr, assetType);

    // timeout
    if (lastUpdate == 0 &&
            (RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3 || now - nAssetSyncStarted > MASTERNODE_SYNC_TIMEOUT * 5)) {
        if (sporkManager.IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
            LogPrintf("CMasternodeSync::Process - ERROR - Sync has failed, will retry later\n");
            RequestedMasternodeAssets = MASTERNODE_SYNC_FAILED;
            RequestedMasternodeAttempt = 0;
            lastFailure = now;
            nCountFailures++;
        } else {
            GetNextAsset();
        }
        return SyncStatus::FAIL;
    }

    if (RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3) return SyncStatus::FAIL;
    return SyncStatus::REQUEST_SYNC;
}
bool CMasternodeSync::MasternodeListIsSynced(CNode* pnode, const int64_t now)
{
    if (RequestedMasternodeAssets == MASTERNODE_SYNC_LIST)
    {
        const SyncStatus status = SyncAssets(pnode,now,lastMasternodeList,"mnsync");
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
                mnodeman.DsegUpdate(pnode);
                RequestedMasternodeAttempt++;
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
bool CMasternodeSync::MasternodeWinnersListIsSync(CNode* pnode, const int64_t now)
{
    if (RequestedMasternodeAssets == MASTERNODE_SYNC_MNW)
    {
        const SyncStatus status = SyncAssets(pnode,now,lastMasternodeWinner,"mnwsync");
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
                CBlockIndex* pindexPrev = chainActive.Tip();
                if (pindexPrev == NULL) return false;

                int nMnCount = mnodeman.size();
                pnode->PushMessage("mnget", nMnCount); //sync payees
                RequestedMasternodeAttempt++;
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
    const int64_t now = GetTime();
    LogPrint("masternode", "Masternode sync process at %lld\n", now);

    if(ShouldWaitForSync(now)) return;

    LogPrint("masternode", "CMasternodeSync::Process() - RequestedMasternodeAssets %d\n", RequestedMasternodeAssets);

    if (RequestedMasternodeAssets == MASTERNODE_SYNC_INITIAL) GetNextAsset();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if (!networkIsRegtest && !IsBlockchainSynced() && RequestedMasternodeAssets > MASTERNODE_SYNC_SPORKS) return;

    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    std::vector<CNode*> vSporkSyncedNodes;

    std::copy_if(std::begin(vNodes), std::end(vNodes), std::back_inserter(vSporkSyncedNodes), [](const CNode *node) {
        return node->fInbound || node->AreSporksSynced();
    });

    // don't event attemp to sync if we don't have 3 synced nodes
    if(vSporkSyncedNodes.size() < 3) {
        return;
    }

    for(CNode* pnode: vSporkSyncedNodes)
    {
        //set to synced
        if (RequestedMasternodeAssets == MASTERNODE_SYNC_SPORKS)
        {
            // this has to be safe to do, because we will get here only if we have 3 peers
            if (RequestedMasternodeAttempt >= 2) GetNextAsset();
            RequestedMasternodeAttempt++;

            return;
        }

        if (pnode->nVersion >= masternodePayments.GetMinMasternodePaymentsProto())
        {
            if(!MasternodeListIsSynced(pnode,now))
            {
                return;
            }

            if (!MasternodeWinnersListIsSync(pnode,now))
            {
                return;
            }
        }
    }
}
