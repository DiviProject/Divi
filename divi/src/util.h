// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Server/client environment: argument handling, config file parsing,
 * logging, thread wrappers
 */
#ifndef BITCOIN_UTIL_H
#define BITCOIN_UTIL_H

#if defined(HAVE_CONFIG_H)
#include "config/divi-config.h"
#endif

#include "compat.h"
#include "tinyformat.h"
#include "utiltime.h"

#include <exception>
#include <map>
#include <stdint.h>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>
#include <boost/thread/exceptions.hpp>

#include <Logging.h>
#include <DataDirectory.h>
#include <ThreadManagementHelpers.h>
//DIVI only features

class Settings;

extern bool fLiteMode;
extern bool fEnableSwiftTX;
extern int nSwiftTXDepth;
extern int nAnonymizeDiviAmount;
extern int nLiquidityProvider;
extern int64_t enforceMasternodePaymentsTime;
extern int keysLoaded;
extern bool fSucessfullyLoaded;
extern bool fLogIPs;

void SetupEnvironment();
void FileCommit(FILE* fileout);
bool TruncateFile(FILE* file, unsigned int length);
int RaiseFileDescriptorLimit(int nMinFD);
void AllocateFileRange(FILE* file, unsigned int offset, unsigned int length);
bool RenameOver(boost::filesystem::path src, boost::filesystem::path dest);
#ifndef WIN32
boost::filesystem::path GetPidFile(const Settings& settings);
void CreatePidFile(const boost::filesystem::path& path, pid_t pid);
#endif
boost::filesystem::path GetTempPath();
void runCommand(std::string strCommand);

inline bool IsSwitchChar(char c)
{
#ifdef WIN32
    return c == '-' || c == '/';
#else
    return c == '-';
#endif
}

/**
 * Format a string to be used as group of options in help messages
 *
 * @param message Group name (e.g. "RPC server options:")
 * @return the formatted string
 */
std::string HelpMessageGroup(const std::string& message);

/**
 * Format a string to be used as option description in help messages
 *
 * @param option Option message (e.g. "-rpcuser=<user>")
 * @param message Option description (e.g. "Username for JSON-RPC connections")
 * @return the formatted string
 */
std::string HelpMessageOpt(const std::string& option, const std::string& message);

#endif // BITCOIN_UTIL_H
