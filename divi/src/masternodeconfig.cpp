// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "masternodeconfig.h"
#include "ui_interface.h"
#include <boost/foreach.hpp>
#include <Settings.h>
#include <DataDirectory.h>
#include <Logging.h>
#include <primitives/transaction.h>

// clang-format on

bool CMasternodeConfig::CMasternodeEntry::parseInputReference(COutPoint& outp) const
{
    outp.hash = uint256S(getTxHash());

    try {
        outp.n = std::stoi(getOutputIndex().c_str());
    } catch (const std::exception& e) {
        LogPrintf("%s: %s on getOutputIndex\n", __func__, e.what());
        return false;
    }

    return true;
}

namespace
{

boost::filesystem::path GetMasternodeConfigFile(const Settings& settings)
{
    boost::filesystem::path pathConfigFile(settings.GetArg("-mnconf", "masternode.conf"));
    if (!pathConfigFile.is_complete()) pathConfigFile = GetDataDir() / pathConfigFile;
    return pathConfigFile;
}

} // anonymous namespace

void CMasternodeConfig::add(const std::string& alias, const std::string& ip, const std::string& privKey,
                            const std::string& txHash, const std::string& outputIndex)
{
    entries.emplace_back(alias, ip, privKey, txHash, outputIndex);
}

bool CMasternodeConfig::read(const Settings& settings, std::string& strErr)
{
    int linenumber = 1;
    boost::filesystem::path pathMasternodeConfigFile = GetMasternodeConfigFile(settings);
    boost::filesystem::ifstream streamConfig(pathMasternodeConfigFile);

    if (!streamConfig.good()) {
        FILE* configFile = fopen(pathMasternodeConfigFile.string().c_str(), "a");
        if (configFile != NULL) {
            std::string strHeader = "# Masternode config file\n"
                                    "# Format: alias IP:port masternodeprivkey collateral_output_txid collateral_output_index\n"
                                    "# Example: mn1 127.0.0.2:51472 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";
            fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
            fclose(configFile);
        }
        return true; // Nothing to read, so just return
    }

    for (std::string line; std::getline(streamConfig, line); linenumber++) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string comment, alias, ip, privKey, txHash, outputIndex;

        if (iss >> comment) {
            if (comment.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }

        if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
            iss.str(line);
            iss.clear();
            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                strErr = translate("Could not parse masternode.conf") + "\n" +
                         strprintf(translate("Line: %d"), linenumber) + "\n\"" + line + "\"";
                streamConfig.close();
                return false;
            }
        }

        add(alias, ip, privKey, txHash, outputIndex);
    }

    streamConfig.close();
    return true;
}

CMasternodeConfig::CMasternodeConfig(
    ): entries()
{
}

const std::vector<CMasternodeConfig::CMasternodeEntry>& CMasternodeConfig::getEntries() const
{
    return entries;
}

int CMasternodeConfig::getCount() const
{
    int c = -1;
    for (const auto& e : entries) {
        if (e.getAlias() != "") c++;
    }
    return c;
}
