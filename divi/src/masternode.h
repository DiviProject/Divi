// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Copyright (c) 2017-2018 The Divi Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_H
#define MASTERNODE_H

#include "base58.h"
#include "init.h"
#include "key.h"
#include "main.h"
#include "net.h"
#include "sync.h"
#include "timedata.h"
#include "util.h"
#include "wallet.h"

#define MASTERNODE_CHECK_SECONDS 5
#define MASTERNODE_EXPIRATION_SECONDS (120 * 60)
#define MASTERNODE_MIN_CONFIRMATIONS 15
#define MASTERNODE_PING_SECONDS (5 * 60)

using namespace std;

class CMasternodePing
{
public:
	string address;
	int blockHeight;
	uint256 blockHash;
	int64_t sigTime;
	vector<unsigned char> vchSig;

	ADD_SERIALIZE_METHODS;

	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
	{
		READWRITE(address);
		READWRITE(blockHeight);
		READWRITE(blockHash);
		READWRITE(sigTime);
		READWRITE(vchSig);
	}

	uint256 GetHash() const
	{
		CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
		ss << address << blockHeight << blockHash << sigTime << vchSig;
		return ss.GetHash();
	}
	void Relay() { CInv inv(MSG_MASTERNODE_PING, GetHash()); RelayInv(inv); }
	string ToString() { return address + to_string(blockHeight) + blockHash.ToString(); }

	friend bool operator==(const CMasternodePing& a, const CMasternodePing& b) { return a.GetHash() == b.GetHash(); }
	friend bool operator!=(const CMasternodePing& a, const CMasternodePing& b) { return !(a.GetHash() == b.GetHash()); }
};

class CMnTier {
public:
	string name;
	CAmount collateral;
	int premium;
	int seesawBasis;

	ADD_SERIALIZE_METHODS;

	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
	{
		READWRITE(name);
		READWRITE(collateral);
		READWRITE(premium);
		READWRITE(seesawBasis);
	}

	string ToString() { return name + to_string(collateral) + to_string(seesawBasis); }
};

const CMnTier Diamond{ "diamond", 10000000 * COIN, 120, 2000 };
const CMnTier Platinum{ "platinum", 3000000 * COIN, 115, 2000 };
const CMnTier Gold{ "gold", 1000000 * COIN, 110, 2000 };
const CMnTier Silver{ "silver", 300000 * COIN, 105, 2000 };
const CMnTier Copper{ "copper", 100000 * COIN, 100, 2000 };

class CMnFunding
{
public:
	CAmount amount;
	CTxIn vin;
	string payAddress;
	string voteAddress;
	vector<unsigned char> sig;

	ADD_SERIALIZE_METHODS;

	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
	{
		READWRITE(amount);
		READWRITE(vin);
		READWRITE(payAddress);
		READWRITE(voteAddress);
		READWRITE(sig);
	}

	bool CheckVin(string address);
	string ToString() { return to_string(amount) + vin.ToString() + payAddress + voteAddress; }
};

class CMasternode
{
private:
	int64_t lastTimeChecked;

public:
	string address = "uninitialized";		// used as the masternode ID
	int protocolVersion;
	CMnTier tier;
	vector<CMnFunding> funding;
	CService service;
	int64_t sigTime;						// mnb message time
	vector<unsigned char> vchSig;			// ----------- broadcast line -----------
	CMasternodePing lastPing;
	int nScanningErrorCount;
	int nLastScanningErrorBlockHeight;
	int64_t upSince;						// updated on add only
	int64_t lastFunded;						// updated on add or update
	int64_t lastPaid;						// updated on add or payment
	int lastVoted;
	vector<int> voteRank;					// stores most recent payment vote ranks for payment vote validity checking

	ADD_SERIALIZE_METHODS;

	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
	{
		READWRITE(address);
		READWRITE(protocolVersion);
		READWRITE(tier);
		READWRITE(funding);
		READWRITE(service);
		READWRITE(sigTime);
		READWRITE(vchSig);
		READWRITE(lastPing);
		READWRITE(nScanningErrorCount);
		READWRITE(nLastScanningErrorBlockHeight);
	}

	uint256 GetHash()
	{
		CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
		ss << address << protocolVersion << tier << funding << service << sigTime << vchSig;
		return ss.GetHash();
	}
	void Relay() { CInv inv(MSG_MASTERNODE_ANNOUNCE, GetHash()); RelayInv(inv); }
	string FundingString() {string fs = ""; for (vector<CMnFunding>::iterator it = funding.begin(); it != funding.end(); it++) fs += (*it).ToString(); return fs; }
	string ToString() { return address + to_string(protocolVersion) + tier.ToString() + FundingString() + service.ToString() + to_string(sigTime); }

	// friend bool operator==(const CMasternode& a, const CMasternode& b) { return a.GetHash() == b.GetHash(); }
	// friend bool operator!=(const CMasternode& a, const CMasternode& b) { return !(a.GetHash() == b.GetHash()); }

	bool IsEnabled();
	string ReadDBs();
	string SignMsg(string msg, vector<unsigned char>& vchSig) { return(SignMsg(address, msg, vchSig)); }
	string SignMsg(string address, string msg, vector<unsigned char>& vchSig);
	string StartUp();
	string VerifyFunding();
	string VerifyMsg(string strAddress, string msg, vector<unsigned char>& vchSig);
	string WriteDBs();
};
#endif
