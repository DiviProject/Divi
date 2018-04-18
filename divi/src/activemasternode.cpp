// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "addrman.h"
#include "masternode.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "protocol.h"
#include "spork.h"

//
// Bootup the Masternode, look for a 10000 DIVI input and register on the network
//
void CActiveMasternode::ManageStatus()
{
	if (!fMasterNode) return;
	if (fDebug) LogPrintf("CActiveMasternode::ManageStatus() - Begin\n");

	me.activeState = MASTERNODE_NOT_CAPABLE;
	if (pwalletMain->IsLocked()) { LogPrintf("CActiveMasternode::ManageStatus() - not capable: Wallet is locked.\n"); return; }
	if (strMasterNodeAddr.empty()) { LogPrintf("CActiveMasternode::ManageStatus() - not capable: No external address\n"); return; }
	me.addr = CService(strMasterNodeAddr);

	LogPrintf("CActiveMasternode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());
	CNode* pnode = ConnectNode((CAddress)service, NULL, false);
	if (!pnode) { LogPrintf("CActiveMasternode::ManageStatus() - not capable: Could not connect to %s\n", service.ToString()); return; }
	pnode->Release();

	if (!me.VerifyFunding()) { LogPrintf("CActiveMasternode::ManageStatus() - Funding verification failed!\n", notCapableReason); return; }

	if (!Register(vin, service, keyMasternode, pubKeyMasternode, errorMessage)) {
		notCapableReason = "Error on Register: " + errorMessage;
		LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
		return;
	}

	LogPrintf("CActiveMasternode::ManageStatus() - Is capable master node!\n");
	status = ACTIVE_MASTERNODE_STARTED;

	if (!SendMasternodePing(errorMessage)) { LogPrintf("CActiveMasternode::ManageStatus() - Error on Ping: %s\n", errorMessage); }
}

bool CActiveMasternode::SendMasternodePing(std::string& errorMessage)
{
	CPubKey pubKeyMasternode;
	CKey keyMasternode;

	if (!obfuScationSigner.SetKey(strMasterNodePrivKey, errorMessage, keyMasternode, pubKeyMasternode)) {
		errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
		return false;
	}

	LogPrintf("CActiveMasternode::SendMasternodePing() - Relay Masternode Ping vin = %s\n", vin.ToString());

	CMasternodePing mnp(vin);
	if (!mnp.Sign(keyMasternode, pubKeyMasternode)) {
		errorMessage = "Couldn't sign Masternode Ping";
		return false;
	}

	// Update lastPing for our masternode in Masternode list
	CMasternode* pmn = mnodeman.Find(vin);
	if (pmn != NULL) {
		if (pmn->IsPingedWithin(MASTERNODE_PING_SECONDS, mnp.sigTime)) {
			errorMessage = "Too early to send Masternode Ping";
			return false;
		}

		pmn->lastPing = mnp;
		mnodeman.mapSeenMasternodePing.insert(make_pair(mnp.GetHash(), mnp));

		//mnodeman.mapSeenMasternodeBroadcast.lastPing is probably outdated, so we'll update it
		CMasternodeBroadcast mnb(*pmn);
		uint256 hash = mnb.GetHash();
		if (mnodeman.mapSeenMasternodeBroadcast.count(hash)) mnodeman.mapSeenMasternodeBroadcast[hash].lastPing = mnp;

		mnp.Relay();
		return true;
	}
	else {
		// Seems like we are trying to send a ping while the Masternode is not registered in the network
		errorMessage = "Obfuscation Masternode List doesn't include our Masternode, shutting down Masternode pinging service! " + vin.ToString();
		status = ACTIVE_MASTERNODE_NOT_CAPABLE;
		notCapableReason = errorMessage;
		return false;
	}
}

bool CActiveMasternode::Register(std::string& errorMessage)
{
	//need correct blocks to send ping
	if (!masternodeSync.IsBlockchainSynced()) {
		errorMessage = GetStatus();
		LogPrintf("CActiveMasternode::Register() - %s\n", errorMessage);
		return false;
	}

	addrman.Add(CAddress(me.addr), CNetAddr("127.0.0.1"), 2 * 60 * 60);

	CMasternodeBroadcast mnb;
	CMasternodePing mnp(me.pubKeyMasternode);
	if (!mnp.Sign(keyMasternode, pubKeyMasternode)) {
		errorMessage = strprintf("Failed to sign ping, vin: %s", vin.ToString());
		LogPrintf("CActiveMasternode::Register() -  %s\n", errorMessage);
		return false;
	}
	mnodeman.mapSeenMasternodePing.insert(make_pair(mnp.GetHash(), mnp));

	LogPrintf("CActiveMasternode::Register() - Adding to Masternode list\n    service: %s\n    vin: %s\n", service.ToString(), vin.ToString());
	mnb = CMasternodeBroadcast(service, vin, pubKeyMasternode, PROTOCOL_VERSION);
	mnb.lastPing = mnp;
	if (!mnb.Sign(keyMasternode)) {
		errorMessage = strprintf("Failed to sign broadcast, vin: %s", vin.ToString());
		LogPrintf("CActiveMasternode::Register() - %s\n", errorMessage);
		return false;
	}
	mnodeman.mapSeenMasternodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));
	masternodeSync.AddedMasternodeList(mnb.GetHash());

	CMasternode* pmn = mnodeman.Find(vin);
	if (pmn == NULL) {
		CMasternode mn(mnb);
		mnodeman.Add(mn);
	}
	else {
		pmn->UpdateFromNewBroadcast(mnb);
	}

	//send to all peers
	LogPrintf("CActiveMasternode::Register() - RelayElectionEntry vin = %s\n", vin.ToString());
	mnb.Relay();
	return true;
}

