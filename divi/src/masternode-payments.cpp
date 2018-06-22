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

bool CMasternodePayments::AddPaymentVote(CPaymentVote& winner)
{
	for (int tier = 0; tier < NUM_TIERS; tier++) {
		if (!mVotes.count(winner.nBlockHeight)) mVotes[winner.nBlockHeight] = CBlockVotes();
		mVotes[winner.nBlockHeight].AddVote(tier, winner.addressPayee[tier]);
	}
	if (!mMnVotes.count(winner.nBlockHeight)) mMnVotes[winner.nBlockHeight] = set<uint256>{ { winner.GetHash() } };		// local (for better garbage collection)
	else mMnVotes[winner.nBlockHeight].insert(winner.GetHash());
	mapSeenPaymentVote[winner.GetHash()] = winner;																		// standard
	winner.Relay();
}

void CMasternodePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake)
{
	return;				// All masternode payments are disabled for beta until ready to test	

	int divi[NUM_TIERS], mNodeCoins = 0;
	for (int tier = 0; tier < NUM_TIERS; tier++) {
		divi[tier] = mnodeman.tierCount[(mnodeman.currHeight + 1) % 15][tier] * Tiers[tier].collateral;
		mNodeCoins += divi[tier];
	}
	int raw[NUM_TIERS], rawTotal = 0;
	for (int tier = 0; tier < NUM_TIERS; tier++) {
		raw[tier] = mNodeCoins * Tiers[tier].seesawBasis * Tiers[tier].premium / divi[tier];
		rawTotal += raw[tier];
	}
	CAmount blockValue = 1075;
	CAmount nMoneySupply = chainActive.Tip()->nMoneySupply;
	CAmount masternodePayment = blockValue * nMoneySupply / (mNodeCoins * 4);
		
	if (fProofOfStake) {
		unsigned int i = txNew.vout.size();
		txNew.vout.resize(i + NUM_TIERS);
		for (int tier = 0; tier < NUM_TIERS; tier++) {
			string payee = mVotes[mnodeman.currHeight + 1].MostVotes(tier);
			if (payee == "") continue;
			txNew.vout[i + tier].scriptPubKey = GetScriptForDestination(CBitcoinAddress(payee).Get());
			txNew.vout[i + tier].nValue = masternodePayment * raw[tier]/rawTotal;
		}
		txNew.vout[i - 1].nValue -= masternodePayment;
	}
	//else {
	//	txNew.vout.resize(2);
	//	txNew.vout[1].scriptPubKey = scriptPubKey;
	//	txNew.vout[1].nValue = masternodePayment;
	//	txNew.vout[0].nValue = blockValue - masternodePayment;
	//}
}

bool CMasternodePayments::IsBlockPayeeValid(const CBlock& block, int nBlockHeight)
{
	return true;		// All masternode payments are disabled for beta until ready to test			

	CTransaction txNew = (nBlockHeight > Params().LAST_POW_BLOCK() ? block.vtx[1] : block.vtx[0]);

	for (int tier = 0; tier < NUM_TIERS; tier++) {
		// require at least MNPAYMENTS_SIGNATURES_REQUIRED votes or approve *anything*
		if (mVotes.count(mnodeman.currHeight + 1) == 0) return true;
		string payee = mVotes[mnodeman.currHeight + 1].Winner(tier);
		if (payee == "") return true;

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
		if (mapSeenPaymentVote.count(winner.GetHash())) return;																	// seen before
		if (winner.nBlockHeight > mnodeman.currHeight + 12) return;																// too far in future
		if (!mnodeman.Find(winner.addressVoter)) { mnodeman.AskForMN(pfrom, winner.addressVoter);  return; }					// unknown masternode; ask for it
		CMasternode* voter = mnodeman.Find(winner.addressVoter);
		if (voter->voteRank[mnodeman.currHeight % 15] > MNPAYMENTS_SIGNATURES_TOTAL) {											// not in top 10 
			if (voter->voteRank[mnodeman.currHeight % 15] > MNPAYMENTS_SIGNATURES_TOTAL * 2) Misbehaving(pfrom->GetId(), 20);
			return;
		}
		if (voter->lastVoted >= mnodeman.currHeight) { Misbehaving(pfrom->GetId(), 20); return; }								// already voted differently
		if (mnodeman.my->VerifyMsg(winner.addressVoter, winner.ToString(), winner.vchSig) != "") return;						// bad vote signature
		AddPaymentVote(winner);
		masternodeSync.AddedMasternodeWinner(winner.GetHash());
		LogPrintf("ProcessMsgPayments mnw SUCCESS\n");
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

