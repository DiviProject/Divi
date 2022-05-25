#include <LicenseAndInfo.h>

#if defined(HAVE_CONFIG_H)
#include "config/divi-config.h"
#endif

#include <chainparams.h>
#include <defaultValues.h>
#include <Settings.h>
#include <tinyformat.h>
#include <utilmoneystr.h>
#include <utilstrencodings.h>
#include <version.h>

#include <boost/thread.hpp>

//! if set, all keys will be derived by using BIP39/BIP44
#define translate(x) std::string(x)

static const int screenWidth = 79;
static const int optIndent = 2;
static const int msgIndent = 7;

std::string HelpMessageGroup(const std::string &message) {
    return std::string(message) + std::string("\n\n");
}

std::string HelpMessageOpt(const std::string &option, const std::string &message) {
    return std::string(optIndent,' ') + std::string(option) +
           std::string("\n") + std::string(msgIndent,' ') +
           FormatParagraph(message, screenWidth - msgIndent, msgIndent) +
           std::string("\n\n");
}

std::string HelpMessageCli()
{
    std::string strUsage;
    strUsage += HelpMessageGroup(translate("Options:"));
    strUsage += HelpMessageOpt("-?", translate("This help message"));
    strUsage += HelpMessageOpt("-conf=<file>", strprintf(translate("Specify configuration file (default: %s)"), "divi.conf"));
    strUsage += HelpMessageOpt("-datadir=<dir>", translate("Specify data directory"));
    strUsage += HelpMessageOpt("-testnet", translate("Use the test network"));
    strUsage += HelpMessageOpt("-regtest", translate("Enter regression test mode, which uses a special chain in which blocks can be "
                                             "solved instantly. This is intended for regression testing tools and app development."));
    strUsage += HelpMessageOpt("-rpcconnect=<ip>", strprintf(translate("Send commands to node running on <ip> (default: %s)"), "127.0.0.1"));
    strUsage += HelpMessageOpt("-rpcport=<port>", strprintf(translate("Connect to JSON-RPC on <port> (default: %u or testnet: %u)"), 51473, 51475));
    strUsage += HelpMessageOpt("-rpcwait", translate("Wait for RPC server to start"));
    strUsage += HelpMessageOpt("-rpcuser=<user>", translate("Username for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcpassword=<pw>", translate("Password for JSON-RPC connections"));

    return strUsage;
}

std::string HelpMessage(HelpMessageMode mode,const Settings& settings, const CChainParams& defaultParameters)
{
    // When adding new options to the categories, please keep and ensure alphabetical ordering.
    std::string strUsage = HelpMessageGroup(translate("Options:"));
    strUsage += HelpMessageOpt("-?", translate("This help message"));
    strUsage += HelpMessageOpt("-version", translate("Print version and exit"));
    strUsage += HelpMessageOpt("-alertnotify=<cmd>", translate("Execute command when a relevant alert is received or we see a really long fork (%s in cmd is replaced by message)"));
    strUsage += HelpMessageOpt("-alerts", strprintf(translate("Receive and display P2P network alerts (default: %u)"), DEFAULT_ALERTS));
    strUsage += HelpMessageOpt("-blocknotify=<cmd>", translate("Execute command when the best block changes (%s in cmd is replaced by block hash)"));
    strUsage += HelpMessageOpt("-checkblocks=<n>", strprintf(translate("How many blocks to check at startup (default: %u, 0 = all)"), 500));
    strUsage += HelpMessageOpt("-conf=<file>", strprintf(translate("Specify configuration file (default: %s)"), "divi.conf"));
    if (mode == HMM_BITCOIND) {
#if !defined(WIN32)
        strUsage += HelpMessageOpt("-daemon", translate("Run in the background as a daemon and accept commands"));
#endif
    }
    strUsage += HelpMessageOpt("-datadir=<dir>", translate("Specify data directory"));
    strUsage += HelpMessageOpt("-dbcache=<n>", strprintf(translate("Set database cache size in megabytes (%d to %d, default: %d)"), MIN_DB_CACHE_SIZE, MAX_DB_CACHE_SIZE, DEFAULT_DB_CACHE_SIZE));
    strUsage += HelpMessageOpt("-loadblock=<file>", translate("Imports blocks from external blk000??.dat file") + " " + translate("on startup"));
    strUsage += HelpMessageOpt("-maxreorg=<n>", strprintf(translate("Set the Maximum reorg depth (default: %u)"),  defaultParameters.MaxReorganizationDepth()   ));
    strUsage += HelpMessageOpt("-maxorphantx=<n>", strprintf(translate("Keep at most <n> unconnectable transactions in memory (default: %u)"), DEFAULT_MAX_ORPHAN_TRANSACTIONS));
    strUsage += HelpMessageOpt("-par=<n>", strprintf(translate("Set the number of script verification threads (%u to %d, 0 = auto, <0 = leave that many cores free, default: %d)"), -(int)boost::thread::hardware_concurrency(), MAX_SCRIPTCHECK_THREADS, DEFAULT_SCRIPTCHECK_THREADS));
#ifndef WIN32
    strUsage += HelpMessageOpt("-pid=<file>", strprintf(translate("Specify pid file (default: %s)"), "divid.pid"));
#endif
    strUsage += HelpMessageOpt("-reindex", translate("Rebuild block chain index from current blk000??.dat files") + " " + translate("on startup"));
    strUsage += HelpMessageOpt("-resync", translate("Delete blockchain folders and resync from scratch") + " " + translate("on startup"));
#if !defined(WIN32)
    strUsage += HelpMessageOpt("-sysperms", translate("Create new files with system default permissions, instead of umask 077 (only effective with disabled wallet functionality)"));
#endif
    strUsage += HelpMessageOpt("-txindex", strprintf(translate("Maintain a full transaction index, used by the getrawtransaction rpc call (default: %u)"), 0));
    strUsage += HelpMessageOpt("-addressindex", strprintf(translate("Maintain a full address index, used to query for the balance, txids and unspent outputs for addresses (default: %u)"), DEFAULT_ADDRESSINDEX));
    strUsage += HelpMessageOpt("-forcestart", translate("Attempt to force blockchain corruption recovery") + " " + translate("on startup"));

    strUsage += HelpMessageGroup(translate("Connection options:"));
    strUsage += HelpMessageOpt("-addnode=<ip>", translate("Add a node to connect to and attempt to keep the connection open"));
    strUsage += HelpMessageOpt("-banscore=<n>", strprintf(translate("Threshold for disconnecting misbehaving peers (default: %u)"), 100));
    strUsage += HelpMessageOpt("-bantime=<n>", strprintf(translate("Number of seconds to keep misbehaving peers from reconnecting (default: %u)"), 86400));
    strUsage += HelpMessageOpt("-bind=<addr>", translate("Bind to given address and always listen on it. Use [host]:port notation for IPv6"));
    strUsage += HelpMessageOpt("-connect=<ip>", translate("Connect only to the specified node(s)"));
    strUsage += HelpMessageOpt("-discover", translate("Discover own IP address (default: 1 when listening and no -externalip)"));
    strUsage += HelpMessageOpt("-dns", translate("Allow DNS lookups for -addnode, -seednode and -connect") + " " + translate("(default: 1)"));
    strUsage += HelpMessageOpt("-dnsseed", translate("Query for peer addresses via DNS lookup, if low on addresses (default: 1 unless -connect)"));
    strUsage += HelpMessageOpt("-externalip=<ip>", translate("Specify your own public address"));
    strUsage += HelpMessageOpt("-forcednsseed", strprintf(translate("Always query for peer addresses via DNS lookup (default: %u)"), 0));
    strUsage += HelpMessageOpt("-listen", translate("Accept connections from outside (default: 1 if no -proxy or -connect)"));
    strUsage += HelpMessageOpt("-listenonion", strprintf(translate("Automatically create Tor hidden service (default: %d)"), DEFAULT_LISTEN_ONION));
    strUsage += HelpMessageOpt("-maxconnections=<n>", strprintf(translate("Maintain at most <n> connections to peers (default: %u)"), 125));
    strUsage += HelpMessageOpt("-maxreceivebuffer=<n>", strprintf(translate("Maximum per-connection receive buffer, <n>*1000 bytes (default: %u)"), 5000));
    strUsage += HelpMessageOpt("-maxsendbuffer=<n>", strprintf(translate("Maximum per-connection send buffer, <n>*1000 bytes (default: %u)"), 1000));
    strUsage += HelpMessageOpt("-onion=<ip:port>", strprintf(translate("Use separate SOCKS5 proxy to reach peers via Tor hidden services (default: %s)"), "-proxy"));
    strUsage += HelpMessageOpt("-onlynet=<net>", translate("Only connect to nodes in network <net> (ipv4, ipv6 or onion)"));
    strUsage += HelpMessageOpt("-permitbaremultisig", strprintf(translate("Relay non-P2SH multisig (default: %u)"), 1));
    strUsage += HelpMessageOpt("-peerbloomfilters", strprintf(translate("Support filtering of blocks and transaction with bloom filters (default: %u)"), DEFAULT_PEERBLOOMFILTERS));
    strUsage += HelpMessageOpt("-port=<port>", strprintf(translate("Listen for connections on <port> (default: %u or testnet: %u)"), 51472, 51474));
    strUsage += HelpMessageOpt("-proxy=<ip:port>", translate("Connect through SOCKS5 proxy"));
    strUsage += HelpMessageOpt("-proxyrandomize", strprintf(translate("Randomize credentials for every proxy connection. This enables Tor stream isolation (default: %u)"), 1));
    strUsage += HelpMessageOpt("-seednode=<ip>", translate("Connect to a node to retrieve peer addresses, and disconnect"));
    strUsage += HelpMessageOpt("-timeout=<n>", strprintf(translate("Specify connection timeout in milliseconds (minimum: 1, default: %d)"), DEFAULT_CONNECT_TIMEOUT));
    strUsage += HelpMessageOpt("-torcontrol=<ip>:<port>", strprintf(translate("Tor control port to use if onion listening enabled (default: %s)"), std::string(DEFAULT_TOR_CONTROL) ));
    strUsage += HelpMessageOpt("-torpassword=<pass>", translate("Tor control port password (default: empty)"));
#ifdef USE_UPNP
#if USE_UPNP
    strUsage += HelpMessageOpt("-upnp", translate("Use UPnP to map the listening port (default: 1 when listening)"));
#else
    strUsage += HelpMessageOpt("-upnp", strprintf(translate("Use UPnP to map the listening port (default: %u)"), 0));
#endif
#endif
    strUsage += HelpMessageOpt("-whitebind=<addr>", translate("Bind to given address and whitelist peers connecting to it. Use [host]:port notation for IPv6"));
    strUsage += HelpMessageOpt("-whitelist=<netmask>", translate("Whitelist peers connecting from the given netmask or IP address. Can be specified multiple times.") +
        " " + translate("Whitelisted peers cannot be DoS banned and their transactions are always relayed, even if they are already in the mempool, useful e.g. for a gateway"));


#ifdef ENABLE_WALLET
    strUsage += HelpMessageGroup(translate("Wallet options:"));
    strUsage += HelpMessageOpt("-monthlybackups=<n>", translate("Number of automatic (monthly) wallet backups (default: 12)"));
    strUsage += HelpMessageOpt("-createwalletbackups=<n>", translate("Number of automatic wallet backups (default: 20)"));
    strUsage += HelpMessageOpt("-disablewallet", translate("Do not load the wallet and disable wallet RPC calls"));
    strUsage += HelpMessageOpt("-keypool=<n>", strprintf(translate("Set key pool size to <n> (default: %u)"), 100));
   strUsage += HelpMessageOpt("-rescan", translate("Rescan the block chain for missing wallet transactions") + " " + translate("on startup"));
    strUsage += HelpMessageOpt("-salvagewallet", translate("Attempt to recover private keys from a corrupt wallet.dat") + " " + translate("on startup"));
    strUsage += HelpMessageOpt("-sendfreetransactions", strprintf(translate("Send transactions as zero-fee transactions if possible (default: %u)"), 0));
    strUsage += HelpMessageOpt("-spendzeroconfchange", strprintf(translate("Spend unconfirmed change when sending transactions (default: %u)"), false));
    strUsage += HelpMessageOpt("-mnemonic", translate("User defined mnemonic for HD wallet (bip39). Only has effect during wallet creation/first start (default: randomly generated)"));
    strUsage += HelpMessageOpt("-mnemonicpassphrase", translate("User defined mnemonic passphrase for HD wallet (BIP39). Only has effect during wallet creation/first start (default: empty string)"));
    strUsage += HelpMessageOpt("-hdseed", translate("User defined seed for HD wallet (should be in hex). Only has effect during wallet creation/first start (default: randomly generated)"));
    if (settings.GetBoolArg("-help-debug", false))
        strUsage += HelpMessageOpt("-allowunencryptedwallet", translate("Do not warn about an unencrypted HD wallet"));
    strUsage += HelpMessageOpt("-disablesystemnotifications", strprintf(translate("Disable OS notifications for incoming transactions (default: %u)"), 0));
    strUsage += HelpMessageOpt("-txconfirmtarget=<n>", strprintf(translate("If paytxfee is not set, include enough fee so transactions begin confirmation on average within n blocks (default: %u)"), 1));
    strUsage += HelpMessageOpt("-maxtxfee=<amt>", strprintf(translate("Maximum total fees to use in a single wallet transaction, setting too low may abort large transactions (default: %s)"),
        FormatMoney(DEFAULT_TRANSACTION_MAXFEE)));
    strUsage += HelpMessageOpt("-upgradewallet", translate("Upgrade wallet to latest format") + " " + translate("on startup"));
    strUsage += HelpMessageOpt("-wallet=<file>", translate("Specify wallet file (within data directory)") + " " + strprintf(translate("(default: %s)"), "wallet.dat"));
    strUsage += HelpMessageOpt("-walletnotify=<cmd>", translate("Execute command when a wallet transaction changes (%s in cmd is replaced by TxID)"));
    if (mode == HMM_BITCOIN_QT)
        strUsage += HelpMessageOpt("-windowtitle=<name>", translate("Wallet window title"));
#endif

#if ENABLE_ZMQ
    strUsage += HelpMessageGroup(translate("ZeroMQ notification options:"));
    strUsage += HelpMessageOpt("-zmqpubhashblock=<address>", translate("Enable publish hash block in <address>"));
    strUsage += HelpMessageOpt("-zmqpubhashtx=<address>", translate("Enable publish hash transaction in <address>"));
    strUsage += HelpMessageOpt("-zmqpubrawblock=<address>", translate("Enable publish raw block in <address>"));
    strUsage += HelpMessageOpt("-zmqpubrawtx=<address>", translate("Enable publish raw transaction in <address>"));
#endif

    strUsage += HelpMessageGroup(translate("Debugging/Testing options:"));
    if (settings.GetBoolArg("-help-debug", false)) {
        strUsage += HelpMessageOpt("-checkblockindex", strprintf("Do a full consistency check for mapBlockIndex, setBlockIndexCandidates, chainActive and mapBlocksUnlinked occasionally. Also sets -checkmempool (default: %u)",defaultParameters.DefaultConsistencyChecks() ));
        strUsage += HelpMessageOpt("-checkmempool=<n>", strprintf("Run checks every <n> transactions (default: %u)", defaultParameters.DefaultConsistencyChecks()));
        strUsage += HelpMessageOpt("-checkpoints", strprintf(translate("Only accept block chain matching built-in checkpoints (default: %u)"), 1));
        strUsage += HelpMessageOpt("-dblogsize=<n>", strprintf(translate("Flush database activity from memory pool to disk log every <n> megabytes (default: %u)"), 100));
        strUsage += HelpMessageOpt("-disablesafemode", strprintf(translate("Disable safemode, override a real safe mode event (default: %u)"), 0));
        strUsage += HelpMessageOpt("-testsafemode", strprintf(translate("Force safe mode (default: %u)"), 0));
        strUsage += HelpMessageOpt("-dropmessagestest=<n>", translate("Randomly drop 1 of every <n> network messages"));
        strUsage += HelpMessageOpt("-fuzzmessagestest=<n>", translate("Randomly fuzz 1 of every <n> network messages"));
        strUsage += HelpMessageOpt("-flushwallet", strprintf(translate("Run a thread to flush wallet periodically (default: %u)"), 1));
        strUsage += HelpMessageOpt("-maxreorg", strprintf(translate("Use a custom max chain reorganization depth (default: %u)"), 100));
        strUsage += HelpMessageOpt("-protocolversion", strprintf(translate("Use a custom protocol version (default: use latest version %d)"), PROTOCOL_VERSION));
        strUsage += HelpMessageOpt("-activeversion", translate("Use a custom active version"));
        strUsage += HelpMessageOpt("-stopafterblockimport", strprintf(translate("Stop running after importing blocks from disk (default: %u)"), 0));
        strUsage += HelpMessageOpt("-sporkkey=<privkey>", translate("Enable spork administration functionality with the appropriate private key."));
    }
    std::string debugCategories = "addrman, alert, bench, coindb, db, lock, rand, rpc, selectcoins, tor, mempool, net, proxy, divi, (obfuscation, swiftx, masternode, mnpayments, mnbudget, zero)"; // Don't translate these and qt below
    if (mode == HMM_BITCOIN_QT)
        debugCategories += ", qt";
    strUsage += HelpMessageOpt("-debug=<category>", strprintf(translate("Output debugging information (default: %u, supplying <category> is optional)"), 0) + ". " +
        translate("If <category> is not supplied, output all debugging information.") + translate("<category> can be:") + " " + debugCategories + ".");
    if (settings.GetBoolArg("-help-debug", false))
        strUsage += HelpMessageOpt("-nodebug", "Turn off debugging messages, same as -debug=0");
#ifdef ENABLE_WALLET
    strUsage += HelpMessageOpt("-mining", strprintf(translate("Generate coins (default: %u)"), 0));
    strUsage += HelpMessageOpt("-genproclimit=<n>", strprintf(translate("Set the number of threads for coin generation if enabled (-1 = all cores, default: %d)"), 1));
#endif
    strUsage += HelpMessageOpt("-help-debug", translate("Show all debugging options (usage: --help -help-debug)"));
    strUsage += HelpMessageOpt("-logips", strprintf(translate("Include IP addresses in debug output (default: %u)"), 0));
    strUsage += HelpMessageOpt("-logtimestamps", strprintf(translate("Prepend debug output with timestamp (default: %u)"), 1));
    if (settings.GetBoolArg("-help-debug", false)) {
        strUsage += HelpMessageOpt("-limitfreerelay=<n>", strprintf(translate("Continuously rate-limit free transactions to <n>*1000 bytes per minute (default:%u)"), 15));
        strUsage += HelpMessageOpt("-relaypriority", strprintf(translate("Require high priority for relaying free or low-fee transactions (default:%u)"), 1));
        strUsage += HelpMessageOpt("-maxsigcachesize=<n>", strprintf(translate("Limit size of signature cache to <n> entries (default: %u)"), 50000));
        strUsage += HelpMessageOpt("-acceptnonstandard", translate("Relay non-standard transactions"));
    }
    strUsage += HelpMessageOpt("-minrelaytxfee=<amt>", strprintf(translate("Fees (in DIV/Kb) smaller than this are considered zero fee for relaying (default: %s)"), FormatMoney( DEFAULT_TX_RELAY_FEE_PER_KILOBYTE )));
    strUsage += HelpMessageOpt("-printtoconsole", strprintf(translate("Send trace/debug info to console instead of debug.log file (default: %u)"), 0));
    if (settings.GetBoolArg("-help-debug", false)) {
        strUsage += HelpMessageOpt("-printpriority", strprintf(translate("Log transaction priority and fee per kB when mining blocks (default: %u)"), 0));
        strUsage += HelpMessageOpt("-privdb", strprintf(translate("Sets the DB_PRIVATE flag in the wallet db environment (default: %u)"), 1));
        strUsage += HelpMessageOpt("-regtest", translate("Enter regression test mode, which uses a special chain in which blocks can be solved instantly.") + " " +
            translate("This is intended for regression testing tools and app development.") + " " +
            translate("In this mode -genproclimit controls how many blocks are generated immediately."));
    }
    strUsage += HelpMessageOpt("-shrinkdebugfile", translate("Shrink debug.log file on client startup (default: 1 when no -debug)"));
    strUsage += HelpMessageOpt("-testnet", translate("Use the test network"));
    strUsage += HelpMessageOpt("-litemode=<n>", strprintf(translate("Disable all DIVI specific functionality (Masternodes, SwiftX, Budgeting) (0-1, default: %u)"), 0));

#ifdef ENABLE_WALLET
    strUsage += HelpMessageGroup(translate("Staking options:"));
    strUsage += HelpMessageOpt("-staking=<n>", strprintf(translate("Enable staking functionality (0-1, default: %u)"), 1));
    if (settings.GetBoolArg("-help-debug", false)) {
        strUsage += HelpMessageOpt("-printstakemodifier", translate("Display the stake modifier calculations in the debug.log file."));
        strUsage += HelpMessageOpt("-printcoinstake", translate("Display verbose coin stake messages in the debug.log file."));
    }
#endif

    strUsage += HelpMessageGroup(translate("Masternode options:"));
    strUsage += HelpMessageOpt("-masternode=<n>", strprintf(translate("Enable the client to act as a masternode (0-1, default: %u)"), 0));
    strUsage += HelpMessageOpt("-mnconf=<file>", strprintf(translate("Specify masternode configuration file (default: %s)"), "masternode.conf"));
    strUsage += HelpMessageOpt("-mnconflock=<n>", strprintf(translate("Lock masternodes from masternode configuration file (default: %u)"), 1));
    strUsage += HelpMessageOpt("-masternodeprivkey=<n>", translate("Set the masternode private key"));
    strUsage += HelpMessageOpt("-masternodeaddr=<n>", strprintf(translate("Set external address:port to get to this masternode (example: %s)"), "128.127.106.235:51472"));
    strUsage += HelpMessageOpt("-budgetvotemode=<mode>", translate("Change automatic finalized budget voting behavior. mode=auto: Vote for only exact finalized budget match to my generated budget. (string, default: auto)"));

    strUsage += HelpMessageGroup(translate("Node relay options:"));
    strUsage += HelpMessageOpt("-datacarrier", strprintf(translate("Relay and mine data carrier transactions (default: %u)"), 1));
    strUsage += HelpMessageOpt("-datacarriersize", strprintf(translate("Maximum size of data in data carrier transactions we relay and mine (default: %u)"), MAX_OP_META_RELAY));
    if (settings.GetBoolArg("-help-debug", false)) {
        strUsage += HelpMessageOpt("-blockversion=<n>", "Override block version to test forking scenarios");
    }

    strUsage += HelpMessageGroup(translate("Block creation options:"));
    strUsage += HelpMessageOpt("-blockminsize=<n>", strprintf(translate("Set minimum block size in bytes (default: %u)"), 0));
    strUsage += HelpMessageOpt("-blockmaxsize=<n>", strprintf(translate("Set maximum block size in bytes (default: %d)"), DEFAULT_BLOCK_MAX_SIZE));
    strUsage += HelpMessageOpt("-blockprioritysize=<n>", strprintf(translate("Set maximum size of high-priority/low-fee transactions in bytes (default: %d)"), DEFAULT_BLOCK_PRIORITY_SIZE));

    strUsage += HelpMessageGroup(translate("RPC server options:"));
    strUsage += HelpMessageOpt("-server", translate("Accept command line and JSON-RPC commands"));
    strUsage += HelpMessageOpt("-rest", strprintf(translate("Accept public REST requests (default: %u)"), 0));
    strUsage += HelpMessageOpt("-rpcbind=<addr>", translate("Bind to given address to listen for JSON-RPC connections. Use [host]:port notation for IPv6. This option can be specified multiple times (default: bind to all interfaces)"));
    strUsage += HelpMessageOpt("-rpcuser=<user>", translate("Username for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcpassword=<pw>", translate("Password for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcport=<port>", strprintf(translate("Listen for JSON-RPC connections on <port> (default: %u or testnet: %u)"), 51473, 51475));
    strUsage += HelpMessageOpt("-rpcallowip=<ip>", translate("Allow JSON-RPC connections from specified source. Valid for <ip> are a single IP (e.g. 1.2.3.4), a network/netmask (e.g. 1.2.3.4/255.255.255.0) or a network/CIDR (e.g. 1.2.3.4/24). This option can be specified multiple times"));
    strUsage += HelpMessageOpt("-rpcthreads=<n>", strprintf(translate("Set the number of threads to service RPC calls (default: %d)"), 4));
    strUsage += HelpMessageOpt("-rpckeepalive", strprintf(translate("RPC support for HTTP persistent connections (default: %d)"), 1));

    return strUsage;
}

std::string LicenseInfo()
{
    return FormatParagraph(strprintf(translate("Copyright (C) 2009-%i The Bitcoin Core Developers"), COPYRIGHT_YEAR)) + "\n" +
           "\n" +
           FormatParagraph(strprintf(translate("Copyright (C) 2014-%i The Dash Core Developers"), COPYRIGHT_YEAR)) + "\n" +
           "\n" +
           FormatParagraph(strprintf(translate("Copyright (C) 2015-%i The DIVI Core Developers"), COPYRIGHT_YEAR)) + "\n" +
           "\n" +
           FormatParagraph(translate("This is experimental software.")) + "\n" +
           "\n" +
           FormatParagraph(translate("Distributed under the MIT software license, see the accompanying file COPYING or <http://www.opensource.org/licenses/mit-license.php>.")) + "\n" +
           "\n" +
           FormatParagraph(translate("This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit <https://www.openssl.org/> and cryptographic software written by Eric Young and UPnP software written by Thomas Bernard.")) +
           "\n";
}
