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
	map<string, int> mBVotes;
	void AddVote(string address, int votes = 1) { if (!mBVotes.count(address)) mBVotes[address] = votes; else mBVotes[address] += votes; }
	int GetVotes(string address) { if (!mBVotes.count(address)) return 0; return mBVotes[address]; }
	string MostVotes() { int maxVotes = 0; string payee = "";
		for (map<string, int>::iterator it = mBVotes.begin(); it != mBVotes.end(); it++)
			if (maxVotes < (*it).second) { payee = (*it).first;  maxVotes = (*it).second; };
		return (payee);
	}
	string Winner() { string best = MostVotes(); if (GetVotes(best) >= MNPAYMENTS_SIGNATURES_REQUIRED) return best; else return ""; }
	uint256 GetHash() { CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION); ss << mBVotes; return ss.GetHash(); }
	ADD_SERIALIZE_METHODS;
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) { READWRITE(mBVotes); }
};

class CPaymentVote						// relayed data structure
{
public:
	string addressVoter;
	int nBlockHeight;
	string addressPayee;
	vector<unsigned char> vchSig;

	uint256 GetHash()
	{
		CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
		ss << addressVoter << nBlockHeight << addressPayee;
		return ss.GetHash();
	}

	void Relay() { CInv inv(MSG_MASTERNODE_WINNER, GetHash()); RelayInv(inv); }

	ADD_SERIALIZE_METHODS;

	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
	{
		READWRITE(addressVoter);
		READWRITE(nBlockHeight);
		READWRITE(addressPayee);
		READWRITE(vchSig);
	}

	string ToString() { return addressVoter + to_string(nBlockHeight) + addressPayee; }
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
