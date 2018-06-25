// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode-payments.h"
#include "addrman.h"
#include "masternode-budget.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "script/standard.h"
#include "sync.h"
#include "util.h"

using namespace std;

CMasternodePayments mnPayments;

void CMasternodePayments::AddPaymentVote(CPaymentVote& winner)
{
	for (int tier = 0; tier < NUM_TIERS; tier++) {
		if (!mVotes.count(winner.nBlockHeight)) mVotes[winner.nBlockHeight] = CBlockVotes();
		mVotes[winner.nBlockHeight].AddVote(tier, winner.addressPayee[tier]);
	}
	if (!mMnVotes.count(winner.nBlockHeight)) mMnVotes[winner.nBlockHeight] = set<uint256>{ { winner.GetHash() } };		// local (for better garbage collection)
	else mMnVotes[winner.nBlockHeight].insert(winner.GetHash());
	mapSeenPaymentVote[winner.GetHash()] = winner;																		// standard
	masternodeSync.AddedMasternodeWinner(winner.GetHash());
	winner.Relay();
}

void CMasternodePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake)
{
	LogPrintStr("CMasternodePayments::FillBlockPayee \n");
	CAmount divi[NUM_TIERS], mNodeCoins = 0;
	for (int tier = 0; tier < NUM_TIERS; tier++) {
		divi[tier] = mnodeman.tierCount[tier] * Tiers[tier].collateral;
		if (divi[tier] == 0) divi[tier]++;
		mNodeCoins += divi[tier];
	}
	CAmount raw[NUM_TIERS], rawTotal = 0;
	for (int tier = 0; tier < NUM_TIERS; tier++) {
		raw[tier] = mNodeCoins * Tiers[tier].seesawBasis * Tiers[tier].premium / divi[tier];
		rawTotal += raw[tier];
	}
	CAmount blockValue = 1075;
	CAmount nMoneySupply = chainActive.Tip()->nMoneySupply / COIN;
	CAmount masternodePayment = (nMoneySupply / (mNodeCoins * 4)) * blockValue;
	if (masternodePayment > 1075) masternodePayment = 1000;

	string mnwinner, payee;
	unsigned int i = txNew.vout.size();
	if (!fProofOfStake) i = 1;
	txNew.vout.resize(i + NUM_TIERS);
	CAmount totalMasterPaid = 0;
	for (int tier = 0; tier < NUM_TIERS; tier++) {
		if (mVotes.count(mnodeman.currHeight + 1)) {
			mnwinner = mVotes[mnodeman.currHeight + 1].MostVotes(tier);
			if (mnwinner != "") payee = mnodeman.Find(mnwinner)->funding[0].payAddress; else payee = "";
		}
		if (payee == "") payee = "DJ2fYZXocM7uWXBNXffyF7QKWfBP4xYQ2b";
		txNew.vout[i + tier].scriptPubKey = GetScriptForDestination(CBitcoinAddress(payee).Get());
		txNew.vout[i + tier].nValue = COIN * masternodePayment * raw[tier] / rawTotal;
		totalMasterPaid += txNew.vout[i + tier].nValue;
	}
	txNew.vout[i - 1].nValue = (COIN * 1075) - totalMasterPaid;
	LogPrintStr("CMasternodePayments::FillBlockPayee END!!! \n");
}

bool CMasternodePayments::IsBlockPayeeValid(const CBlock& block, int nBlockHeight)
{
	LogPrintStr("CMasternodePayments::IsBlockPayeeValid");

	CTransaction txNew = (nBlockHeight > Params().LAST_POW_BLOCK() ? block.vtx[1] : block.vtx[0]);

	// if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
	if (mVotes.count(mnodeman.currHeight + 1) < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

	for (int tier = 0; tier < NUM_TIERS; tier++) {
		string payee = mVotes[mnodeman.currHeight + 1].Winner(tier);
		if (payee == "") continue;

		CScript scriptPubKey = GetScriptForDestination(CBitcoinAddress(payee).Get());
		CAmount nReward = GetBlockValue(mnodeman.currHeight);
		CAmount requiredMasternodePayment = GetMasternodePayment(mnodeman.currHeight, nReward);

		bool found = false;
		for (vector<CTxOut>::iterator it = txNew.vout.begin(); !found && it != txNew.vout.end(); it++)
			if ((*it).scriptPubKey == scriptPubKey && (*it).nValue >= requiredMasternodePayment) found = true;
		if (!found) { LogPrint("masternode", "Invalid mn payment detected %s\n", txNew.ToString().c_str()); return false; }
	}
	return true;
}


void CMasternodePayments::ProcessMsgPayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
	if (!masternodeSync.IsBlockchainSynced()) return;

	if (strCommand == "mnget") { //Masternode Payments Request Sync
		LogPrintf("ProcessMsgPayments mnget reply START\n");
		int nCountNeeded;
		vRecv >> nCountNeeded;

		if (Params().NetworkID() == CBaseChainParams::MAIN) {
			if (pfrom->HasFulfilledRequest("mnget")) {
				LogPrint("masternode", "mnget - peer already asked me for the list\n");
				Misbehaving(pfrom->GetId(), 20);
				return;
			}
		}

		pfrom->FulfilledRequest("mnget");
		mnPayments.Sync(pfrom, nCountNeeded);
		LogPrint("mnpayments", "mnget - Sent Masternode winners to peer %i\n", pfrom->GetId());
	}
	else if (strCommand == "mnw") { // Masternode Payment Vote 
		LogPrintf("ProcessMsgPayments mnw START\n");
		CPaymentVote winner;
		vRecv >> winner;
		if (mapSeenPaymentVote.count(winner.GetHash())) { LogPrintStr("mnw - seen before"); return; }							// seen before
		if (winner.nBlockHeight > mnodeman.currHeight + 12) { LogPrintStr("mnw - too far in future"); return; }					// too far in future
		if (!mnodeman.Find(winner.addressVoter)) { mnodeman.AskForMN(pfrom, winner.addressVoter);  return; }					// unknown masternode; ask for it
		CMasternode* voter = mnodeman.Find(winner.addressVoter);
		if (voter->voteRank[mnodeman.currHeight % 15] > MNPAYMENTS_SIGNATURES_TOTAL) {											// not in top 10 
			LogPrintStr("ProcessMsgPayments mnw NOT IN TOP 10!!!!!!!!! \n");
			if (voter->voteRank[mnodeman.currHeight % 15] > MNPAYMENTS_SIGNATURES_TOTAL * 2) Misbehaving(pfrom->GetId(), 20);
			// return;
		}
		if (voter->lastVoted >= mnodeman.currHeight) { Misbehaving(pfrom->GetId(), 20); return; }								// already voted differently
		if (mnodeman.my->VerifyMsg(winner.addressVoter, winner.ToString(), winner.vchSig) != "") return;						// bad vote signature
		AddPaymentVote(winner);
		LogPrintf("\n ProcessMsgPayments mnw SUCCESS\n");
	}
}

bool CMasternodePayments::IsScheduled(string address)
{
	for (int i = 0; mVotes.count(mnodeman.currHeight + i) > 0; i++)
		if (mVotes[mnodeman.currHeight + i].MostVotes(mnodeman.Find(address)->tier.ordinal) == address) return true;
	return false;
}

void CMasternodePayments::Sync(CNode* node, int nCountNeeded)
{
	int nInvCount = 0;
	for (map<int, CBlockVotes>::iterator it = mVotes.begin(); it != mVotes.end(); it++)
		if ((*it).first >= mnodeman.currHeight - nCountNeeded) {
			node->PushInventory(CInv(MSG_MASTERNODE_WINNER, (*it).second.GetHash()));
			nInvCount++;
		}
	node->PushMessage("ssc", MASTERNODE_SYNC_MNW, nInvCount);
}

