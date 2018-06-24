// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Copyright (c) 2017-2018 The Divi Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODEMAN_H
#define MASTERNODEMAN_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "masternode.h"
#include "net.h"
#include "sync.h"
#include "util.h"

#define MASTERNODES_DSEG_SECONDS (3 * 60 * 60)
#define MASTERNODES_DUMP_SECONDS (15 * 60)
#define MASTERNODE_MIN_MNB_SECONDS (5 * 60)
#define MN_WINNER_MINIMUM_AGE 8							// Age in seconds, currently 8000 ~ 2.25 hours, should be > MASTERNODE_EXPIRATION_SECONDS
#define MIN_PEER_PROTO_VERSION 0

using namespace std;

class CMasternodeMan
{
private:
    mutable CCriticalSection cs;							// critical section to protect the inner data structures
    mutable CCriticalSection cs_process_message;			// critical section to protect the inner data structures specifically on messaging

    map<CNetAddr, int64_t> mAskedUs4List;					// who's asked for the Masternode list and the last time
    map<CNetAddr, int64_t> mWeAsked4List;					// who we asked for the Masternode list and the last time
    map<string, int64_t> mWeAsked4Entry;					// which Masternodes we've asked for

public:
	map<string, uint256> mAddress2MnHash;					// maps divi address key to masternode hashes
	map<uint256, CMasternode> mMasternodes;					// Keep track of all *current* masternodes by hash
	map<uint256, CMasternodePing> mSeenPings;				// Keep track of all *current* pings by hash
	vector<pair<uint256, string>> vCurrScores[NUM_TIERS];	// ranked masternodes (score to divi address) sorted by lowest-best score ascending
	map<int, uint256> blockHashes;
	int currHeight = 0, nodeCount = 0, stableSize = 0;
	int tierCount[15][NUM_TIERS];
	map<string, int> topSigs[15];
	CMasternode* my;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);
		READWRITE(mAddress2MnHash);
		READWRITE(mMasternodes);
    }

	void Add(CMasternode* mn);
	CMasternode* Find(string address) { if (!mAddress2MnHash.count(address)) return NULL; else return &mMasternodes[mAddress2MnHash[address]]; }
	void Remove(CMasternode* mn);
	void Update(CMasternode* mn);
	void UpdatePing(CMasternodePing* mnp);

	void CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion) { ipv4 = ipv6 = onion = 0; }
	int size() { return mMasternodes.size(); }

	void AskForMN(CNode* pnode, string& address);
	void DsegUpdate(CNode* pnode);
	void ProcessBlock();
	void ProcessMasternodeConnectionsX();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
};

extern CMasternodeMan mnodeman;

#endif
