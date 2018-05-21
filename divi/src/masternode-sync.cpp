// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "activemasternode.h"
#include "masternode-sync.h"
#include "masternode-payments.h"
#include "masternode.h"
#include "masternodeman.h"
#include "spork.h"
#include "util.h"
#include "addrman.h"

class CMasternodeSync;
CMasternodeSync masternodeSync;

CMasternodeSync::CMasternodeSync()
{
	Reset();
}

bool CMasternodeSync::IsSynced()
{
	return RequestedMasternodeAssets == MASTERNODE_SYNC_FINISHED;
}

bool CMasternodeSync::IsBlockchainSynced()
{
	static bool fBlockchainSynced = false;
	static int64_t lastProcess = GetTime();

	// if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
	if (GetTime() - lastProcess > 60 * 60) {
		Reset();
		fBlockchainSynced = false;
	}
	lastProcess = GetTime();

	if (fBlockchainSynced) return true;

	if (fImporting || fReindex) return false;

	TRY_LOCK(cs_main, lockMain);
	if (!lockMain) return false;

	CBlockIndex* pindex = chainActive.Tip();
	if (pindex == NULL) return false;


	if (pindex->nTime + 60 * 60 < GetTime())
		return false;

	fBlockchainSynced = true;

	return true;
}

void CMasternodeSync::Reset()
{
	lastMasternodeList = 0;
	lastMasternodeWinner = 0;
	lastFailure = 0;
	nCountFailures = 0;
	sumMasternodeList = 0;
	sumMasternodeWinner = 0;
	sumBudgetItemProp = 0;
	sumBudgetItemFin = 0;
	countMasternodeList = 0;
	countMasternodeWinner = 0;
	countBudgetItemProp = 0;
	countBudgetItemFin = 0;
	RequestedMasternodeAssets = MASTERNODE_SYNC_INITIAL;
	RequestedMasternodeAttempt = 0;
	nAssetSyncStarted = GetTime();
}


void CMasternodeSync::GetNextAsset()
{
	switch (RequestedMasternodeAssets) {
	case (MASTERNODE_SYNC_INITIAL):
	case (MASTERNODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
		ClearFulfilledRequest();
		RequestedMasternodeAssets = MASTERNODE_SYNC_LIST;
		break;
	//case (MASTERNODE_SYNC_SPORKS):
	//	RequestedMasternodeAssets = MASTERNODE_SYNC_LIST;
	//	break;
	case (MASTERNODE_SYNC_LIST):
		RequestedMasternodeAssets = MASTERNODE_SYNC_MNW;
		break;
	case (MASTERNODE_SYNC_MNW):
		RequestedMasternodeAssets = MASTERNODE_SYNC_FINISHED;
		break;
	}
	RequestedMasternodeAttempt = 0;
	nAssetSyncStarted = GetTime();
}

std::string CMasternodeSync::GetSyncStatus()
{
	switch (masternodeSync.RequestedMasternodeAssets) {
	case MASTERNODE_SYNC_INITIAL:
		return _("Synchronization pending...");
	case MASTERNODE_SYNC_SPORKS:
		return _("Synchronizing sporks...");
	case MASTERNODE_SYNC_LIST:
		return _("Synchronizing masternodes...");
	case MASTERNODE_SYNC_MNW:
		return _("Synchronizing masternode winners...");
	case MASTERNODE_SYNC_FAILED:
		return _("Synchronization failed");
	case MASTERNODE_SYNC_FINISHED:
		return _("Synchronization finished");
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
			sumMasternodeList += nCount;
			countMasternodeList++;
			break;
		case (MASTERNODE_SYNC_MNW):
			if (nItemID != RequestedMasternodeAssets) return;
			sumMasternodeWinner += nCount;
			countMasternodeWinner++;
			break;
		}

		LogPrint("masternode", "CMasternodeSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
	}
}

void CMasternodeSync::ClearFulfilledRequest()
{
	TRY_LOCK(cs_vNodes, lockRecv);
	if (!lockRecv) return;

	for (vector<CNode*>::iterator it = vNodes.begin(); it != vNodes.end(); it++) {
		CNode* pnode = (*it);
		pnode->ClearFulfilledRequest("getspork");
		pnode->ClearFulfilledRequest("mnsync");
		pnode->ClearFulfilledRequest("mnwsync");
	}
}

void CMasternodeSync::Process()
{
	static int tick = 0;

	if (tick++ % MASTERNODE_SYNC_TIMEOUT != 0) return;

	if (IsSynced()) {
		/* Resync if we lose all masternodes from sleep/wake or failure to sync originally */
		if (mnodeman.mMasternodes.size() == 0) Reset(); else return;
	}

	//try syncing again
	if (RequestedMasternodeAssets == MASTERNODE_SYNC_FAILED && lastFailure + (1 * 60) < GetTime()) {
		Reset();
	}
	else if (RequestedMasternodeAssets == MASTERNODE_SYNC_FAILED) return;
	
	LogPrint("masternode", "CMasternodeSync::Process() - tick %d RequestedMasternodeAssets %d\n", tick, RequestedMasternodeAssets);

	if (RequestedMasternodeAssets == MASTERNODE_SYNC_INITIAL) GetNextAsset();

	// sporks synced but blockchain is not, wait until we're almost at a recent block to continue
	if (Params().NetworkID() != CBaseChainParams::REGTEST &&
		!IsBlockchainSynced() && RequestedMasternodeAssets > MASTERNODE_SYNC_SPORKS) return;

	TRY_LOCK(cs_vNodes, lockRecv);
	if (!lockRecv) return;

	for (vector<CNode*>::iterator it = vNodes.begin(); it != vNodes.end(); it++) {
		CNode* pnode = (*it);
		if (Params().NetworkID() == CBaseChainParams::REGTEST) {
			if (RequestedMasternodeAttempt <= 2) {
				pnode->PushMessage("getsporks"); //get current network sporks
			}
			else if (RequestedMasternodeAttempt < 4) {
				mnodeman.DsegUpdate(pnode);
			}
			else if (RequestedMasternodeAttempt < 6) {
				int nMnCount = mnodeman.mMasternodes.size();
				pnode->PushMessage("mnget", nMnCount); //sync payees
				uint256 n = 0;
				pnode->PushMessage("mnvs", n); //sync masternode votes
			}
			else {
				RequestedMasternodeAssets = MASTERNODE_SYNC_FINISHED;
			}
			RequestedMasternodeAttempt++;
			return;
		}

		//set to synced
		if (RequestedMasternodeAssets == MASTERNODE_SYNC_SPORKS) {
			if (pnode->HasFulfilledRequest("getspork")) continue;
			pnode->FulfilledRequest("getspork");

			pnode->PushMessage("getsporks"); //get current network sporks
			if (RequestedMasternodeAttempt >= 2) GetNextAsset();
			RequestedMasternodeAttempt++;

			return;
		}

		//		if (pnode->nVersion >= mnPayments.GetMinMasternodePaymentsProto()) {
		if (RequestedMasternodeAssets == MASTERNODE_SYNC_LIST) {
			LogPrint("masternode", "CMasternodeSync::Process() - lastMasternodeList %lld (GetTime() - MASTERNODE_SYNC_TIMEOUT) %lld\n", lastMasternodeList, GetTime() - MASTERNODE_SYNC_TIMEOUT);
			if (lastMasternodeList > 0 && lastMasternodeList < GetTime() - MASTERNODE_SYNC_TIMEOUT * 2 && RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
				GetNextAsset();
				return;
			}

			if (pnode->HasFulfilledRequest("mnsync")) continue;
			pnode->FulfilledRequest("mnsync");

			// timeout
			if (lastMasternodeList == 0 &&
				(RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > MASTERNODE_SYNC_TIMEOUT * 5)) {
				if (IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
					LogPrintf("CMasternodeSync::Process - ERROR - Sync has failed, will retry later\n");
					RequestedMasternodeAssets = MASTERNODE_SYNC_FAILED;
					RequestedMasternodeAttempt = 0;
					lastFailure = GetTime();
					nCountFailures++;
				}
				else {
					GetNextAsset();
				}
				return;
			}

			if (RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3) return;

			mnodeman.DsegUpdate(pnode);
			RequestedMasternodeAttempt++;
			return;
		}

		if (RequestedMasternodeAssets == MASTERNODE_SYNC_MNW) {
			if (lastMasternodeWinner > 0 && lastMasternodeWinner < GetTime() - MASTERNODE_SYNC_TIMEOUT * 2 && RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
				GetNextAsset();
				return;
			}

			if (pnode->HasFulfilledRequest("mnwsync")) continue;
			pnode->FulfilledRequest("mnwsync");

			// timeout
			if (lastMasternodeWinner == 0 &&
				(RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > MASTERNODE_SYNC_TIMEOUT * 5)) {
				if (IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
					LogPrintf("CMasternodeSync::Process - ERROR - Sync has failed, will retry later\n");
					RequestedMasternodeAssets = MASTERNODE_SYNC_FAILED;
					RequestedMasternodeAttempt = 0;
					lastFailure = GetTime();
					nCountFailures++;
				}
				else {
					GetNextAsset();
				}
				return;
			}

			if (RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3) return;

			CBlockIndex* pindexPrev = chainActive.Tip();
			if (pindexPrev == NULL) return;

			int nMnCount = mnodeman.mMasternodes.size();
			pnode->PushMessage("mnget", nMnCount); //sync payees
			RequestedMasternodeAttempt++;

			return;
			//}
		}
	}
}
