// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Copyright (c) 2017-2018 The Divi Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternodeman.h"
#include "activemasternode.h"
#include "addrman.h"
#include "masternode.h"
#include "obfuscation.h"
#include "spork.h"
#include "util.h"

using namespace std;

CMasternodeMan mnodeman;

struct CompareTimes { bool operator()(const pair<int64_t, pair<uint256, string>>& t1, const pair<int64_t, pair<uint256, string>>& t2) const { return t1.first < t2.first; } };
struct CompareScores { bool operator()(const pair<uint256, string>& t1, const pair<uint256, string>& t2) const { return t1.first < t2.first; } };

void CMasternodeMan::Add(CMasternode* mn)
{
	LogPrintf("Add STARTED!\n");
	LOCK(cs);
	mMasternodes[mn->GetHash()] = *mn;
	mAddress2MnHash[mn->address] = mn->GetHash();
	mn->Relay();
	LogPrintf("Add DONE!\n");
}

void CMasternodeMan::Remove(CMasternode* mn)
{
	LOCK(cs);
	mWeAsked4Entry.erase(mn->address);
	mAddress2MnHash.erase(mn->address);
	mMasternodes.erase(mn->GetHash());
}

void CMasternodeMan::Update(CMasternode* mn)
{
	CMasternode* current = Find(mn->address);
	if (current->GetHash() == mn->GetHash()) { LogPrintStr("\n\n\n\n\n ERROR: Matching mnb Hashes \n\n\n\n\n"); return; }
	mWeAsked4Entry.erase(mn->address);
	mMasternodes[mn->GetHash()] = *mn;
	mAddress2MnHash[mn->address] = mn->GetHash();;
	mMasternodes.erase(current->GetHash());
	mn->Relay();
}

void CMasternodeMan::UpdatePing(CMasternodePing* mnp)
{
	CMasternode* current = Find(mnp->address);
	if (current->lastPing.GetHash() == mnp->GetHash()) { LogPrintStr("\n\n\n\n\n ERROR: Matching mnping Hashes \n\n\n\n\n"); return; }
	mSeenPings.erase(current->lastPing.GetHash());
	mSeenPings.insert(make_pair(mnp->GetHash(), *mnp));
	current->lastPing.blockHeight = mnp->blockHeight;
	current->lastPing.blockHash = mnp->blockHash;
	current->lastPing.sigTime = mnp->sigTime;
	current->lastPing.vchSig = mnp->vchSig;
	mnp->Relay();
}


void CMasternodeMan::AskForMN(CNode* pnode, string& address)
{
	if (mWeAsked4Entry.count(address)) {
		if (mWeAsked4Entry[address] > GetAdjustedTime()) return;
	}
	LogPrint("masternode", "CMasternodeMan::AskForMN - Asking node for missing entry %s\n", address);
	pnode->PushMessage("dseg", address);
	mWeAsked4Entry[address] = GetAdjustedTime() + MASTERNODE_MIN_MNB_SECONDS;
}

void CMasternodeMan::DsegUpdate(CNode* pnode)
{
	LOCK(cs);

	if (Params().NetworkID() == CBaseChainParams::MAIN) {
		if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
			std::map<CNetAddr, int64_t>::iterator it = mWeAsked4List.find(pnode->addr);
			if (it != mWeAsked4List.end()) {
				if (GetTime() < (*it).second) {
					LogPrint("masternode", "dseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
					return;
				}
			}
		}
	}

	pnode->PushMessage("dseg", CTxIn());
	int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
	mWeAsked4List[pnode->addr] = askAgain;
}

uint256 DoubleHash(uint256 hash, string address = string()) {
	CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
	ss << hash;
	if (!address.empty()) ss << address;
	return ss.GetHash();
}

void CMasternodeMan::ProcessBlock()
{
	vector<pair<uint256, string>> vVoteScores;
	vector<pair<int64_t, pair<uint256, string>>> vLastPaid[NUM_TIERS];
	vector<pair<int64_t, pair<uint256, string>>> vLastPaid2[NUM_TIERS];

	if (!chainActive.Tip()) return;
	CBlockIndex* currBlock = chainActive.Tip();
	if (currHeight >= currBlock->nHeight) return;
	int currHeight = currBlock->nHeight;

	if (my->lastPing.sigTime + MASTERNODE_PING_SECONDS < GetAdjustedTime()) {
		LogPrintStr("SENDING PING!!!\n");
		my->lastPing.blockHeight = currHeight;
		my->lastPing.blockHash = currBlock->GetBlockHash();
		my->lastPing.sigTime = GetAdjustedTime();
		my->SignMsg(my->lastPing.ToString(), my->lastPing.vchSig);
		UpdatePing(&my->lastPing);
	}

	uint256 currHash = currBlock->GetBlockHash();
	uint256 currHash2 = DoubleHash(currHash);
	for (int i = 0; i < 90 && currBlock->pprev != NULL; i++) { blockHashes[currBlock->nHeight] = currBlock->GetBlockHash();  currBlock = currBlock->pprev; }
	uint256 voteHash = currBlock->GetBlockHash();
	uint256 voteHash2 = DoubleHash(voteHash);

	stableSize = 0;
	for (int i = 0; i < NUM_TIERS; i++) { tierCount[currHeight % 15][i] = 0; vCurrScores[i].clear(); }
	LOCK(cs);
	for (map<uint256, CMasternode>::iterator it = mMasternodes.begin(); it != mMasternodes.end(); ) {
		CMasternode mn = (*it).second;
		if (!(*it).second.IsEnabled()) { LogPrintStr("\n\n\n\nDELETING!!!\n\n\n\n"); mWeAsked4Entry.erase(mn.address); mAddress2MnHash.erase(mn.address); mMasternodes.erase(it++); continue; }
		if (GetAdjustedTime() < mn.sigTime + MN_WINNER_MINIMUM_AGE) { it++;  continue; }
		stableSize++;
		tierCount[currHeight % 15][mn.tier.ordinal]++;
		/* calculate scores */
		uint256 currHashM = DoubleHash(currHash, mn.address);
		vCurrScores[mn.tier.ordinal].push_back(make_pair((currHash2 > currHashM ? currHash2 - currHashM : currHashM - currHash2), mn.address));
		uint256 voteHashM = DoubleHash(voteHash, mn.address);
		uint256 voteScore = (voteHash2 > voteHashM ? voteHash2 - voteHashM : voteHashM - voteHash2);
		if (mn.tier.name == "diamond" || mn.tier.name == "platinum") vVoteScores.push_back(make_pair(voteScore, mn.address));
		// generate payment list
		if (mn.lastFunded + nodeCount <= currHeight && !mnPayments.IsScheduled(mn.address)) {
			if (mn.sigTime + (nodeCount * 2.6 * 60) < GetAdjustedTime()) vLastPaid[mn.tier.ordinal].push_back(make_pair(mn.lastPaid, make_pair(voteScore, mn.address)));
			else vLastPaid2[mn.tier.ordinal].push_back(make_pair(mn.lastPaid, make_pair(voteScore, mn.address)));
		}
		++it;
	}
	nodeCount = mMasternodes.size();
	sort(vCurrScores[0].begin(), vCurrScores[0].end(), CompareScores());
	sort(vVoteScores.begin(), vVoteScores.end(), CompareScores());
	topSigs[currHeight % 15].clear();
	int rank = 0;
	for (vector<pair<uint256, string>>::iterator it = vVoteScores.begin(); it != vVoteScores.end() && rank++ < 2 * MNPAYMENTS_SIGNATURES_TOTAL; it++)
		topSigs[currHeight % 15][(*it).second] = rank;
	if (topSigs[currHeight % 15].count(my->address) && topSigs[currHeight % 15][my->address] <= MNPAYMENTS_SIGNATURES_TOTAL) {	// we are top-ranked group & get to vote 
		for (int tier = 0; tier < NUM_TIERS; tier++) {
			if (vLastPaid[tier].size() < tierCount[currHeight % 15][tier] / 3) vLastPaid[tier].insert(vLastPaid[tier].end(), vLastPaid2[tier].begin(), vLastPaid2[tier].end()); // append recently restarted nodes during upgrades  
			sort(vLastPaid[tier].begin(), vLastPaid[tier].end(), CompareTimes());
			int topTenth = vLastPaid[tier].size() / 10;
			vVoteScores.clear();
			rank = 0;
			for (vector<pair<int64_t, pair<uint256, string>>>::iterator it = vLastPaid[tier].begin(); rank < topTenth; rank++) vVoteScores.push_back((*it).second);
			sort(vVoteScores.begin(), vVoteScores.end(), CompareScores());
		}
		CPaymentVote myVote = CPaymentVote({ my->address, currHeight, vVoteScores[0].second });
		my->SignMsg(myVote.ToString(), myVote.vchSig);
		mnPayments.AddPaymentVote(myVote);
	}

	for (map<CNetAddr, int64_t>::iterator it = mAskedUs4List.begin(); it != mAskedUs4List.end(); ) if ((*it).second < GetTime()) mAskedUs4List.erase(it++); else ++it;
	for (map<CNetAddr, int64_t>::iterator it = mWeAsked4List.begin(); it != mWeAsked4List.end(); ) if ((*it).second < GetTime()) mWeAsked4List.erase(it++); else ++it;
	for (map<string, int64_t>::iterator it = mWeAsked4Entry.begin(); it != mWeAsked4Entry.end(); ) if ((*it).second < GetTime()) mWeAsked4Entry.erase(it++); else ++it;
	LogPrintf("masternodeman.ProcessBlock END\n");
}

void CMasternodeMan::ProcessMasternodeConnectionsX()
{
	//we don't care about this for regtest
	if (Params().NetworkID() == CBaseChainParams::REGTEST) return;

	LOCK(cs_vNodes);
	for (vector<CNode *>::iterator it = vNodes.begin(); it != vNodes.end(); it++) {
		if ((*it)->fObfuScationMaster) {
			if (obfuScationPool.pSubmittedToMasternode != NULL && (*it)->addr == obfuScationPool.pSubmittedToMasternode->service) continue;
			LogPrint("masternode", "Closing Masternode connection peer=%i \n", (*it)->GetId());
			(*it)->fObfuScationMaster = false;
			(*it)->Release();
		}
	}
}

void CMasternodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
	if (fLiteMode) return; //disable all Masternode-related functionality
	if (!masternodeSync.IsBlockchainSynced()) return;
	string errorMsg;

	LOCK(cs_process_message);

	LogPrintStr("\n\n\n\n masternodeman.ProcessMessage START\n");
	LogPrintStr(strCommand);
	if (strCommand == "mnb") { //Masternode Broadcast
		LogPrintf("masternodeman.ProcessMessage mnb START\n");
		CMasternode mnb;
		vRecv >> mnb;
		if (mMasternodes.count(mnb.GetHash())) { LogPrintf("masternodeman.ProcessMessage mnb already seen \n"); return; }		// entirely redundant

		CMasternode* pmn = Find(mnb.address);
		if (pmn && pmn->sigTime > mnb.sigTime) { LogPrint("masternode", "Obsolete Masternode %s\n", mnb.address); return; }	// should never happen, but . . . 
		if (mnb.protocolVersion < MIN_PEER_PROTO_VERSION) { LogPrint("masternode", "Outdated protocol, Masternode %s\n", mnb.address); return; }
		if (mnb.sigTime > GetAdjustedTime()) { LogPrint("masternode", "Future Signature rejected, Masternode %s\n", mnb.address); return; }
		// if ((errorMsg = my->VerifyMsg(mnb.address, mnb.ToString(), mnb.vchSig)) != "") { LogPrint("masternode", "Bad signature %s - %s\n", mnb.address, errorMsg); return; }
		if ((errorMsg = mnb.VerifyFunding()) != "") { LogPrint("masternode", "Masternode %s - %s\n", mnb.address, errorMsg); return; }
		if (!Find(mnb.address)) Add(&mnb); else Update(&mnb);
		LogPrintf("masternodeman.ProcessMessage mnb END\n");
	}
	else if (strCommand == "mnp") { //Masternode Ping
		LogPrintf("masternodeman.ProcessMessage mnp START\n");
		CMasternodePing mnp;
		vRecv >> mnp;
		if (mSeenPings.count(mnp.GetHash())) { LogPrintf("masternodeman.ProcessMessage mnping already seen \n"); return; }		// entirely redundant

		int64_t now = GetAdjustedTime();
		if (mnp.sigTime > now) { LogPrint("masternode", "CMasternodePing - Future signature rejected %s\n", mnp.address); return; }
		if (mnp.sigTime <= now - 60 * 60) { LogPrint("masternode", "CMasternodePing - Signature over an hour old %s\n", mnp.address); return; }
		if (mnp.blockHeight < chainActive.Height() - 24) { LogPrint("masternode", "CMasternodePing - Block height is too old %s\n", mnp.address); return; }
		if (blockHashes[mnp.blockHeight] != mnp.blockHash) { LogPrint("masternode", "CMasternodePing - Block hash is incorrect %s\n", mnp.address); return; }
		LogPrint("masternode", "New Ping - %s - %lli\n", mnp.address, mnp.sigTime);

		CMasternode* mn = Find(mnp.address);
		if (!mn) { AskForMN(pfrom, mnp.address); return; }

		if (now < mn->lastPing.sigTime + MASTERNODE_PING_SECONDS - 60) { LogPrint("masternode", "CMasternodePing - Ping too early %s\n", mnp.address); return; }
		if ((errorMsg = my->VerifyMsg(mnp.address, mnp.ToString(), mnp.vchSig)) != "") { LogPrint("masternode", "Bad Ping Sig %s\n", mnp.address); return; }
		UpdatePing(&mnp);
		LogPrintf("masternodeman.ProcessMessage mnp END\n");
	}
	else if (strCommand == "dseg") { //Get Masternode list or specific entry
		LogPrintf("masternodeman.ProcessMessage dseg START\n");
		string address;
		vRecv >> address;

		if (address == "*") { // should only ask for list rarely
			std::map<CNetAddr, int64_t>::iterator i = mAskedUs4List.find(pfrom->addr);
			if (i != mAskedUs4List.end())
				if (GetAdjustedTime() < (*i).second) { Misbehaving(pfrom->GetId(), 34); LogPrint("masternode", "dseg - peer already asked me for the list\n"); return; }
			mAskedUs4List[pfrom->addr] = GetAdjustedTime() + MASTERNODES_DSEG_SECONDS;
		} //else, asking for a single specific node which is ok

		int nInvCount = 0;
		for (map<uint256, CMasternode>::iterator it = mMasternodes.begin(); it != mMasternodes.end(); it++) {
			LogPrint("masternode", "dseg - Sending Masternode entry - %s \n", (*it).second.address);
			if (address == "*" || address == (*it).second.address) {
				pfrom->PushInventory(CInv(MSG_MASTERNODE_ANNOUNCE, (*it).second.GetHash()));
				nInvCount++;
				if (address == (*it).second.address) { LogPrint("masternode", "dseg - Sent 1 Masternode entry to peer %i\n", pfrom->GetId()); return; }
			}
		}
		if (address == "*") {
			pfrom->PushMessage("ssc", MASTERNODE_SYNC_LIST, nInvCount);
			LogPrint("masternode", "dseg - Sent %d Masternode entries to peer %i\n", nInvCount, pfrom->GetId());
		}
		LogPrintf("masternodeman.ProcessMessage dseg END\n");
	}
}

