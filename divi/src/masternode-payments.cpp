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

static std::vector<CAmount> CalcPaymentAmounts(int nBlockHeight)
{
	CAmount divi[NUM_TIERS], mNodeCoins = 0;
    std::vector<CAmount> tierPayment(NUM_TIERS);
    for (int tier = 0; tier < NUM_TIERS; tier++)
    {
		LogPrintStr("mnodeman.tierCount[tier] = " + to_string(mnodeman.tierCount[tier]));
		divi[tier] = mnodeman.tierCount[tier] * Tiers[tier].collateral;
        if (divi[tier] == 0)
            divi[tier] = Tiers[tier].collateral;

		mNodeCoins += divi[tier];
		LogPrintStr("; mNodeCoins = " + to_string(mNodeCoins) + "\n");
	}
	CAmount raw[NUM_TIERS], rawTotal = 0;
    for (int tier = 0; tier < NUM_TIERS; tier++)
    {
		raw[tier] = mNodeCoins * Tiers[tier].seesawBasis * Tiers[tier].premium / divi[tier];
		rawTotal += raw[tier];
	}

    auto blockValue = GetBlockValue(nBlockHeight, false);
    auto mnPayment = GetMasternodePayment(nBlockHeight, blockValue);

    for (int tier = 0; tier < NUM_TIERS; tier++)
    {
		tierPayment[tier] = mnPayment * raw[tier] / rawTotal * COIN;
		LogPrintStr("tierPayment[tier] = " + to_string(tierPayment[tier]) + "\n");
	}
	LogPrintStr("Wallet balance = " + to_string(pwalletMain->GetBalance()) + "\n");

    return tierPayment;
}

string GetMyAddress(string strAccount = "", bool bForceNew = false)
{
	CWalletDB walletdb(pwalletMain->strWalletFile);

	CAccount account;
	walletdb.ReadAccount(strAccount, account);

	bool bKeyUsed = false;

	// Check if the current key has been used
	if (account.vchPubKey.IsValid()) {
		CScript scriptPubKey = GetScriptForDestination(account.vchPubKey.GetID());
		for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin();
			it != pwalletMain->mapWallet.end() && account.vchPubKey.IsValid();
			++it) {
			const CWalletTx& wtx = (*it).second;
			BOOST_FOREACH(const CTxOut& txout, wtx.vout)
				if (txout.scriptPubKey == scriptPubKey)
					bKeyUsed = true;
		}
	}

	// Generate a new key
	if (!account.vchPubKey.IsValid() || bForceNew || bKeyUsed) {
        if (!pwalletMain->GetKeyFromPool(account.vchPubKey, false)) {
			pwalletMain->TopUpKeyPool(100);
            pwalletMain->GetKeyFromPool(account.vchPubKey, false);
		}
		pwalletMain->SetAddressBook(account.vchPubKey.GetID(), strAccount, "receive");
		walletdb.WriteAccount(strAccount, account);
	}

	return CBitcoinAddress(account.vchPubKey.GetID()).ToString();
}


void CMasternodePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake)
{
	LogPrintStr("CMasternodePayments::FillBlockPayee \n");
	CalcPaymentAmounts(mnodeman.currHeight + 1);

	string mnwinner, payee;
	unsigned int i = txNew.vout.size();
	txNew.vout.resize(i + NUM_TIERS + 2);
    for (int tier = 0; tier < NUM_TIERS; tier++)
    {
        if (mVotes.count(mnodeman.currHeight + 1))
        {
			mnwinner = mVotes[mnodeman.currHeight + 1].MostVotes(tier);
            if (mnwinner != "")
                payee = mnodeman.Find(mnwinner)->funding[0].payAddress;
            else
                continue;
		}
        else
        {
            continue;
        }
		txNew.vout[i + tier].scriptPubKey = GetScriptForDestination(CBitcoinAddress(payee).Get());
//		txNew.vout[i + tier].nValue = tierPayment[tier];
	}
	string dev = "DTh8UsMac7UkrbcQU6uYZEKMtMQZvKBxYz", lottery = "D6q1G8LWJeDqd97qC1su8mc1PhiiD6muZn";
	txNew.vout[i + NUM_TIERS].scriptPubKey = GetScriptForDestination(CBitcoinAddress(dev).Get());					// Dev wallet
	txNew.vout[i + NUM_TIERS].nValue = 125 * COIN;
	txNew.vout[i + NUM_TIERS + 1].scriptPubKey = GetScriptForDestination(CBitcoinAddress(lottery).Get());			// Lottery wallet
	txNew.vout[i + NUM_TIERS + 1].nValue = 50 * COIN;
//	txNew.vout[i - 1].nValue -= totalMasterPaid;
	LogPrintStr("CMasternodePayments::FillBlockPayee END!!! \n\n\n\n\n\n\n");
}

bool CMasternodePayments::IsBlockPayeeValid(const CBlock& block, int nBlockHeight)
{
	LogPrintStr("CMasternodePayments::IsBlockPayeeValid");
	if (nBlockHeight <= Params().LAST_POW_BLOCK()) return true;

	CTransaction txNew = block.vtx[1];
	CalcPaymentAmounts(nBlockHeight);

	for (int tier = 0; tier < NUM_TIERS; tier++) {
		string payee = mVotes[nBlockHeight].Winner(tier);
		if (payee == "") continue;

		CScript scriptPubKey = GetScriptForDestination(CBitcoinAddress(payee).Get());

		bool found = false;
		for (vector<CTxOut>::iterator it = txNew.vout.begin(); !found && it != txNew.vout.end(); it++)
//			if ((*it).scriptPubKey == scriptPubKey && (*it).nValue >= tierPayment[tier] - 5 * COIN) found = true;
		if (!found) { LogPrint("masternode", "Missing payment for tier %s in %s\n", to_string(tier), txNew.ToString().c_str()); return false; }
	}
	return true;
}


void CMasternodePayments::ProcessMsgPayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
	if (!masternodeSync.IsBlockchainSynced()) return;

	if (strCommand == "vshl") { //
		LogPrintf("\n\n\n\n\n ProcessMsgPayments vshl START \n\n");
		string value;
		vRecv >> value;

		LogPrintStr("Value: " + value + "\n\n\n");
	}

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
        if (mapSeenPaymentVote.count(winner.GetHash()))
        {
            // seen before
            LogPrintStr("mnw - seen before");
            return;
        }

        if (winner.nBlockHeight > mnodeman.currHeight + 12)
        {
            // too far in future
            LogPrintStr("mnw - too far in future");
            return;
        }

        if (!mnodeman.Find(winner.addressVoter))
        {
            // unknown masternode; ask for it
            mnodeman.AskForMN(pfrom, winner.addressVoter);
            return;
        }

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

