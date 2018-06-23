// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SRC_MASTERNODECONFIG_H_
#define SRC_MASTERNODECONFIG_H_

#include <string>
#include <vector>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

class CMasternodeEntry
{
public:
	string alias;
	string tier;
	string mnAddress;
	string txHash;
	string outputIndex;
	string payAddress;
	string voteAddress;
	string signature;
};

class CMasternodeConfig
{
public:
	std::vector<CMasternodeEntry> entries;

	CMasternodeConfig() { entries = std::vector<CMasternodeEntry>(); }

	bool read(string& strErr);
	void add(string alias, string tier, string mnAddress, string txHash, string outputIndex, string payAddress, string voteAddress, string signature);
	int getCount() { return entries.size(); }
};

extern CMasternodeConfig masternodeConfig;

#endif /* SRC_MASTERNODECONFIG_H_ */
