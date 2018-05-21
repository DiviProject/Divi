// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "net.h"
#include "masternode.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "util.h"
#include "ui_interface.h"

CMasternodeConfig masternodeConfig;

void CMasternodeConfig::add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex)
{
    CMasternodeEntry cme(alias, ip, privKey, txHash, outputIndex);
    entries.push_back(cme);
}

bool CMasternodeConfig::read(std::string& strErr)
{
	mnodeman.my = new CMasternode();
	int linenumber = 1;
    boost::filesystem::path pathMasternodeConfigFile = GetMasternodeConfigFile();
    boost::filesystem::ifstream streamConfig(pathMasternodeConfigFile);

    if (!streamConfig.good()) {
        FILE* configFile = fopen(pathMasternodeConfigFile.string().c_str(), "a");
        if (configFile != NULL) {
            std::string strHeader = "# Masternode config file\n"
                                    "# Format: alias IP:port masternodeprivkey collateral_output_txid collateral_output_index\n"
                                    "# Example: mn1 127.0.0.2:51474 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";
            fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
            fclose(configFile);
        }
        return true; // Nothing to read, so just return
    }

    for (std::string line; std::getline(streamConfig, line); linenumber++) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string comment, alias, tier, txHash, outputIndex, mnAddress, payAddress, voteAddress, signature;

        if (iss >> comment) {
            if (comment.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }

        if (!(iss >> alias >> tier >> txHash >> outputIndex >> mnAddress >> payAddress >> voteAddress >> signature)) {
            iss.str(line);
            iss.clear();
            if (!(iss >> alias >> tier >> txHash >> outputIndex >> mnAddress >> payAddress >> voteAddress >> signature)) {
                strErr = _("Could not parse masternode.conf") + "\n" + strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
                streamConfig.close();
                return false;
            }
        }

		// fprintf(stderr, "%s %s %s %s", alias.c_str(), tier.c_str(), txHash.c_str(), outputIndex.c_str());
        add(alias, tier, mnAddress, txHash, outputIndex);
		CAmount nAmount;
		if (tier == "diamond") { nAmount = Diamond.collateral; mnodeman.my->tier = Diamond; }
		else if (tier == "platinum") { nAmount = Platinum.collateral; mnodeman.my->tier = Platinum; }
		else if (tier == "gold") { nAmount = Gold.collateral; mnodeman.my->tier = Gold; }
		else if (tier == "silver") { nAmount = Silver.collateral; mnodeman.my->tier = Silver; }
		else if (tier == "copper") { nAmount = Copper.collateral; mnodeman.my->tier = Copper; }
		CTxIn vin = *(new CTxIn(uint256(txHash), atoi(outputIndex)));
		mnodeman.my->funding.push_back(*(new CMnFunding({ nAmount, vin, payAddress, voteAddress, signature })));
		
    }

    streamConfig.close();
    return true;
}

bool CMasternodeConfig::CMasternodeEntry::castOutputIndex(int &n)
{
    try {
        n = std::stoi(outputIndex);
    } catch (const std::exception e) {
        LogPrintf("%s: %s on getOutputIndex\n", __func__, e.what());
        return false;
    }

    return true;
}
