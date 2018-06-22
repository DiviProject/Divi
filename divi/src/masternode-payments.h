// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Copyright (c) 2017-2018 The Divi Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_PAYMENTS_H
#define MASTERNODE_PAYMENTS_H

#include "key.h"
#include "main.h"
#include "masternode.h"

using namespace std;

#define MNPAYMENTS_SIGNATURES_REQUIRED 6
#define MNPAYMENTS_SIGNATURES_TOTAL 10

class CBlockVotes
{
public:
	map<string, int> mBVotes[NUM_TIERS];
	void AddVote(int tier, string address, int votes = 1) { if (!mBVotes[tier].count(address)) mBVotes[tier][address] = votes; else mBVotes[tier][address] += votes; }
	int GetVotes(int tier, string address) { if (!mBVotes[tier].count(address)) return 0; return mBVotes[tier][address]; }
	string MostVotes(int tier) {
		int maxVotes = 0; string payee = "";
		for (map<string, int>::iterator it = mBVotes[tier].begin(); it != mBVotes[tier].end(); it++)
			if (maxVotes < (*it).second) { payee = (*it).first;  maxVotes = (*it).second; };
		return (payee);
	}
	string Winner(int tier) { string best = MostVotes(tier); if (GetVotes(tier, best) >= MNPAYMENTS_SIGNATURES_REQUIRED) return best; else return ""; }
	uint256 GetHash() {
		CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
		for (int tier = 0; tier < NUM_TIERS; tier++) ss << mBVotes[tier];
		return ss.GetHash();
	}
	ADD_SERIALIZE_METHODS;
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) { for (int tier = 0; tier < NUM_TIERS; tier++) READWRITE(mBVotes[tier]); }
};

class CPaymentVote						// relayed data structure
{
public:
	string addressVoter;
	int nBlockHeight;
	string addressPayee[NUM_TIERS];
	vector<unsigned char> vchSig;

	uint256 GetHash()
	{
		CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
		ss << addressVoter << nBlockHeight;
		for (int i = 0; i < NUM_TIERS; i++) ss << addressPayee[i];
		return ss.GetHash();
	}

	void Relay() { CInv inv(MSG_MASTERNODE_WINNER, GetHash()); RelayInv(inv); }

	ADD_SERIALIZE_METHODS;

	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
	{
		READWRITE(addressVoter);
		READWRITE(nBlockHeight);
		for (int i = 0; i < NUM_TIERS; i++) READWRITE(addressPayee[i]);
		READWRITE(vchSig);
	}

	string ToString() {
		string ret = addressVoter + to_string(nBlockHeight);
		for (int i = 0; i < NUM_TIERS; i++) ret += addressPayee[i];
		return ret;
	}
};

class CMasternodePayments
{
private:
	map<int, set<uint256>> mMnVotes;						// block numbers to sets of votes, enables easy garbage collection of mapSeenPaymentVotes
	map<int, CBlockVotes> mVotes;							// block numbers to the actual votes <payee, vote count>

public:
	map<uint256, CPaymentVote> mapSeenPaymentVote;			// Keep track of all masternode broadcasts I've seen

	bool AddPaymentVote(CPaymentVote& winner);									// adds vote to mapMasterNodeBlocks, called from both self-voting & message processing
	void FillBlockPayee(CMutableTransaction& txNew, int64_t Fees, bool PoS);
	bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted) { return nMinted <= nExpectedValue; }
	bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight);
	bool IsScheduled(string address);
	void ProcessMsgPayments(CNode* pfrom, string& strCmd, CDataStream& vRecv);
	void Sync(CNode* node, int nCountNeeded);

	ADD_SERIALIZE_METHODS;

	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
	{
		READWRITE(mMnVotes);
		READWRITE(mVotes);
		READWRITE(mapSeenPaymentVote);
	}
};

extern CMasternodePayments mnPayments;

#endif
