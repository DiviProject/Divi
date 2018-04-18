// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEMASTERNODE_H
#define ACTIVEMASTERNODE_H

#include "init.h"
#include "key.h"
#include "masternode.h"
#include "net.h"
#include "obfuscation.h"
#include "sync.h"
#include "wallet.h"

// Responsible for activating the Masternode and pinging the network
class CActiveMasternode
{
private:
	// critical section to protect the inner data structures
	mutable CCriticalSection cs;

	/// Ping Masternode
	bool SendMasternodePing(std::string& errorMessage);

	/// Register any Masternode
	bool Register(CTxIn vin, CService service, CKey keyMasternode, CPubKey pubKeyMasternode, std::string& errorMessage);

public:
	CMasternode me;

	CActiveMasternode()
	{
		me = CMasternode();
	}

	/// Manage status of main Masternode
	void ManageStatus();
	std::string GetStatus();

	/// Register remote Masternode
	bool Register(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage);
};

#endif
