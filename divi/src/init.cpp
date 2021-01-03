// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/divi-config.h"
#endif

#include "init.h"

#include "activemasternode.h"
#include "addrman.h"
#include "amount.h"
#include "BlockFileOpener.h"
#include "checkpoints.h"
#include "compat/sanity.h"
#include "datacachemanager.h"
#include "key.h"
#include "main.h"
#include "obfuscation.h"
#include "masternodeconfig.h"
#include "activemasternode.h"
#include <walletBackupFeatureContainer.h>
#include "miner.h"
#include "net.h"
#include "rpcserver.h"
#include "script/standard.h"
#include "spork.h"
#include "sporkdb.h"
#include "txdb.h"
#include "torcontrol.h"
#include "ui_interface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "NotificationInterface.h"
#include "FeeAndPriorityCalculator.h"
#include <Settings.h>

#ifdef ENABLE_WALLET
#include "db.h"
#include "wallet.h"
#include "walletdb.h"
#include <WalletTx.h>
#endif

#include <fstream>
#include <stdint.h>
#include <stdio.h>

#ifndef WIN32
#include <signal.h>
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>

#if ENABLE_ZMQ
#include "zmq/zmqnotificationinterface.h"
#endif

#include <ValidationState.h>
#include <verifyDb.h>

#ifdef ENABLE_WALLET
CWallet* pwalletMain = NULL;
int nWalletBackups = 20;

/**
 * Wallet Settings
 */
extern CAmount nTransactionValueMultiplier;
extern unsigned int nTransactionSizeMultiplier;
extern unsigned int nTxConfirmTarget;
extern bool bdisableSystemnotifications;
extern bool fSendFreeTransactions;
extern bool fPayAtLeastCustomFee;
extern CFeeRate payTxFee;
#endif
volatile bool fFeeEstimatesInitialized = false;
volatile bool fRestartRequested = false; // true: restart false: shutdown
extern std::list<uint256> listAccCheckpointsNoDB;
extern Settings& settings;
extern NotificationInterfaceRegistry registry;
extern std::string strMasterNodeAddr;
extern std::string strMasterNodePrivKey;

#if ENABLE_ZMQ
static CZMQNotificationInterface* pzmqNotificationInterface = NULL;
#endif

#ifdef WIN32
// Win32 LevelDB doesn't use filedescriptors, and the ones used for
// accessing block files, don't count towards to fd_set size limit
// anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

/** Used to pass flags to the Bind() function */
enum BindFlags {
    BF_NONE = 0,
    BF_EXPLICIT = (1U << 0),
    BF_REPORT_ERROR = (1U << 1),
    BF_WHITELIST = (1U << 2),
};

std::string errorMsg;
static const char* FEE_ESTIMATES_FILENAME = "fee_estimates.dat";
CClientUIInterface uiInterface;

bool static InitError(const std::string& str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_ERROR);
    return false;
}

bool static InitWarning(const std::string& str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_WARNING);
    return true;
}

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

//
// Thread management and startup/shutdown:
//
// The network-processing threads are all part of a thread group
// created by AppInit() or the Qt main() function.
//
// A clean exit happens when StartShutdown() or the SIGTERM
// signal handler sets fRequestShutdown, which triggers
// the DetectShutdownThread(), which interrupts the main thread group.
// DetectShutdownThread() then exits, which causes AppInit() to
// continue (it .joins the shutdown thread).
// Shutdown() is then
// called to clean up database connections, and stop other
// threads that should only be stopped after the main network-processing
// threads have exited.
//
// Note that if running -daemon the parent process returns from InitializeDivi
// before adding any threads to the threadGroup, so .join_all() returns
// immediately and the parent exits from main().
//
// Shutdown for Qt is very similar, only it uses a QTimer to detect
// fRequestShutdown getting set, and then does the normal Qt
// shutdown thing.
//

volatile bool fRequestShutdown = false;

void StartShutdown()
{
    fRequestShutdown = true;
}
bool ShutdownRequested()
{
    return fRequestShutdown || fRestartRequested;
}

class CCoinsViewErrorCatcher : public CCoinsViewBacked
{
public:
    CCoinsViewErrorCatcher(CCoinsView* view) : CCoinsViewBacked(view) {}
    bool GetCoins(const uint256& txid, CCoins& coins) const
    {
        try {
            return CCoinsViewBacked::GetCoins(txid, coins);
        } catch (const std::runtime_error& e) {
            uiInterface.ThreadSafeMessageBox(translate("Error reading from database, shutting down."), "", CClientUIInterface::MSG_ERROR);
            LogPrintf("Error reading from database: %s\n", e.what());
            // Starting the shutdown sequence and returning false to the caller would be
            // interpreted as 'entry not found' (as opposed to unable to read data), and
            // could lead to invalid interpration. Just exit immediately, as we can't
            // continue anyway, and all writes should be atomic.
            abort();
        }
    }
    // Writes do not need similar protection, as failure to write is handled by the caller.
};

static CCoinsViewDB* pcoinsdbview = NULL;
static CCoinsViewErrorCatcher* pcoinscatcher = NULL;

#ifdef ENABLE_WALLET
inline void FlushWallet(bool shutdown = false) { if(pwalletMain) CDB::bitdb.Flush(shutdown);}
#endif
void FlushWalletAndStopMinting()
{
#ifdef ENABLE_WALLET
    FlushWallet();
    GenerateDivi(false, NULL, 0);
#endif
}

void StoreDataCaches()
{
    DataCacheManager(
        GetDataDir(),
        uiInterface,
        fLiteMode).StoreDataCaches();
}

bool LoadDataCaches()
{
    return DataCacheManager(
        GetDataDir(),
        uiInterface,
        fLiteMode).LoadDataCaches();
}

void SaveFeeEstimatesFromMempool()
{
    if (fFeeEstimatesInitialized) {
        boost::filesystem::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
        CAutoFile est_fileout(fopen(est_path.string().c_str(), "wb"), SER_DISK, CLIENT_VERSION);
        if (!est_fileout.IsNull())
            mempool.WriteFeeEstimates(est_fileout);
        else
            LogPrintf("%s: Failed to write fee estimates to %s\n", __func__, est_path.string());
        fFeeEstimatesInitialized = false;
    }
}

void DeallocateShallowDatabases()
{
    delete pcoinsTip;
    delete pcoinscatcher;
    delete pcoinsdbview;
    delete pblocktree;
    delete pSporkDB;

    pcoinsTip = NULL;
    pcoinscatcher = NULL;
    pcoinsdbview = NULL;
    pblocktree = NULL;
    pSporkDB = NULL;
}

void CleanAndReallocateShallowDatabases(const std::pair<std::size_t,std::size_t>& blockTreeAndCoinDBCacheSizes)
{
    DeallocateShallowDatabases();
    pSporkDB = new CSporkDB(0, false, false);
    pblocktree = new CBlockTreeDB(blockTreeAndCoinDBCacheSizes.first, false, fReindex);
    pcoinsdbview = new CCoinsViewDB(blockTreeAndCoinDBCacheSizes.second, false, fReindex);
    pcoinscatcher = new CCoinsViewErrorCatcher(pcoinsdbview);
    pcoinsTip = new CCoinsViewCache(pcoinscatcher);
}

void FlushStateAndDeallocateShallowDatabases()
{
    LOCK(cs_main);
    if (pcoinsTip != NULL) {
        FlushStateToDisk();
        //record that client took the proper shutdown procedure
        pblocktree->WriteFlag("shutdown", true);
    }
    DeallocateShallowDatabases();
}

/** Preparing steps before shutting down or restarting the wallet */
void PrepareShutdown()
{
    fRequestShutdown = true;  // Needed when we shutdown the wallet
    fRestartRequested = true; // Needed when we restart the wallet
    LogPrintf("%s: In progress...\n", __func__);
    static CCriticalSection cs_Shutdown;
    TRY_LOCK(cs_Shutdown, lockShutdown);
    if (!lockShutdown)
        return;

    /// Note: Shutdown() must be able to handle cases in which InitializeDivi() failed part of the way,
    /// for example if the data directory was found to be locked.
    /// Be sure that anything that writes files or flushes caches only does this if the respective
    /// module was initialized.
    RenameThread("divi-shutoff");
    mempool.AddTransactionsUpdated(1);
    StopRPCThreads();
    FlushWalletAndStopMinting();
    StopNode();
    InterruptTorControl();
    StopTorControl();
    StoreDataCaches();
    UnregisterNodeSignals(GetNodeSignals());
    SaveFeeEstimatesFromMempool();
    FlushStateAndDeallocateShallowDatabases();

#ifdef ENABLE_WALLET
    FlushWallet(true);
#endif
#if ENABLE_ZMQ
    if (pzmqNotificationInterface) {
        UnregisterValidationInterface(pzmqNotificationInterface);
        delete pzmqNotificationInterface;
        pzmqNotificationInterface = NULL;
    }
#endif
#ifndef WIN32
    boost::filesystem::remove(GetPidFile());
#endif

    UnregisterAllValidationInterfaces();
}

/**
* Shutdown is split into 2 parts:
* Part 1: shut down everything but the main wallet instance (done in PrepareShutdown() )
* Part 2: delete wallet instance
*
* In case of a restart PrepareShutdown() was already called before, but this method here gets
* called implicitly when the parent object is deleted. In this case we have to skip the
* PrepareShutdown() part because it was already executed and just delete the wallet instance.
*/
void Shutdown()
{
    // Shutdown part 1: prepare shutdown
    if (!fRestartRequested) {
        PrepareShutdown();
    }

// Shutdown part 2: delete wallet instance
#ifdef ENABLE_WALLET
    delete pwalletMain;
    pwalletMain = NULL;
#endif
    LogPrintf("%s: done\n", __func__);
}

/**
 * Signal handlers are very limited in what they are allowed to do, so:
 */
void HandleSIGTERM(int)
{
    fRequestShutdown = true;
}

void HandleSIGHUP(int)
{
    fReopenDebugLog = true;
}

bool static Bind(const CService& addr, unsigned int flags)
{
    if (!(flags & BF_EXPLICIT) && IsLimited(addr))
        return false;
    std::string strError;
    if (!BindListenPort(addr, strError, (flags & BF_WHITELIST) != 0)) {
        if (flags & BF_REPORT_ERROR)
            return InitError(strError);
        return false;
    }
    return true;
}

std::string HelpMessage(HelpMessageMode mode)
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
    strUsage += HelpMessageOpt("-dbcache=<n>", strprintf(translate("Set database cache size in megabytes (%d to %d, default: %d)"), nMinDbCache, nMaxDbCache, nDefaultDbCache));
    strUsage += HelpMessageOpt("-loadblock=<file>", translate("Imports blocks from external blk000??.dat file") + " " + translate("on startup"));
    strUsage += HelpMessageOpt("-maxreorg=<n>", strprintf(translate("Set the Maximum reorg depth (default: %u)"), Params(CBaseChainParams::MAIN).MaxReorganizationDepth()));
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
    strUsage += HelpMessageOpt("-torcontrol=<ip>:<port>", strprintf(translate("Tor control port to use if onion listening enabled (default: %s)"), DEFAULT_TOR_CONTROL));
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
    if (settings.GetBoolArg("-help-debug", false))
        strUsage += HelpMessageOpt("-mintxfee=<amt>", strprintf(translate("Fees (in DIV/Kb) smaller than this are considered zero fee for transaction creation (default: %s)"),
            FormatMoney(CWallet::minTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-paytxfee=<amt>", strprintf(translate("Fee (in DIV/kB) to add to transactions you send (default: %s)"), FormatMoney(payTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-rescan", translate("Rescan the block chain for missing wallet transactions") + " " + translate("on startup"));
    strUsage += HelpMessageOpt("-salvagewallet", translate("Attempt to recover private keys from a corrupt wallet.dat") + " " + translate("on startup"));
    strUsage += HelpMessageOpt("-sendfreetransactions", strprintf(translate("Send transactions as zero-fee transactions if possible (default: %u)"), 0));
    strUsage += HelpMessageOpt("-spendzeroconfchange", strprintf(translate("Spend unconfirmed change when sending transactions (default: %u)"), false));
    strUsage += HelpMessageOpt("-usehd", translate("Use hierarchical deterministic key generation (HD) after BIP39/BIP44. Only has effect during wallet creation/first start") + " " + strprintf(translate("(default: %u)"), DEFAULT_USE_HD_WALLET));
    strUsage += HelpMessageOpt("-mnemonic", translate("User defined mnemonic for HD wallet (bip39). Only has effect during wallet creation/first start (default: randomly generated)"));
    strUsage += HelpMessageOpt("-mnemonicpassphrase", translate("User defined mnemonic passphrase for HD wallet (BIP39). Only has effect during wallet creation/first start (default: empty string)"));
    strUsage += HelpMessageOpt("-hdseed", translate("User defined seed for HD wallet (should be in hex). Only has effect during wallet creation/first start (default: randomly generated)"));
    if (GetBoolArg("-help-debug", false))
        strUsage += HelpMessageOpt("-allowunencryptedwallet", translate("Do not warn about an unencrypted HD wallet"));
    strUsage += HelpMessageOpt("-disablesystemnotifications", strprintf(translate("Disable OS notifications for incoming transactions (default: %u)"), 0));
    strUsage += HelpMessageOpt("-txconfirmtarget=<n>", strprintf(translate("If paytxfee is not set, include enough fee so transactions begin confirmation on average within n blocks (default: %u)"), 1));
    strUsage += HelpMessageOpt("-maxtxfee=<amt>", strprintf(translate("Maximum total fees to use in a single wallet transaction, setting too low may abort large transactions (default: %s)"),
        FormatMoney(maxTxFee)));
    strUsage += HelpMessageOpt("-upgradewallet", translate("Upgrade wallet to latest format") + " " + translate("on startup"));
    strUsage += HelpMessageOpt("-wallet=<file>", translate("Specify wallet file (within data directory)") + " " + strprintf(translate("(default: %s)"), "wallet.dat"));
    strUsage += HelpMessageOpt("-walletnotify=<cmd>", translate("Execute command when a wallet transaction changes (%s in cmd is replaced by TxID)"));
    if (mode == HMM_BITCOIN_QT)
        strUsage += HelpMessageOpt("-windowtitle=<name>", translate("Wallet window title"));
    strUsage += HelpMessageOpt("-zapwallettxes=<mode>", translate("Delete all wallet transactions and only recover those parts of the blockchain through -rescan on startup") +
        " " + translate("(1 = keep tx meta data e.g. account owner and payment request information, 2 = drop tx meta data)"));
#endif

#if ENABLE_ZMQ
    strUsage += HelpMessageGroup(translate("ZeroMQ notification options:"));
    strUsage += HelpMessageOpt("-zmqpubhashblock=<address>", translate("Enable publish hash block in <address>"));
    strUsage += HelpMessageOpt("-zmqpubhashtx=<address>", translate("Enable publish hash transaction in <address>"));
    strUsage += HelpMessageOpt("-zmqpubhashtxlock=<address>", translate("Enable publish hash transaction (locked via SwiftX) in <address>"));
    strUsage += HelpMessageOpt("-zmqpubrawblock=<address>", translate("Enable publish raw block in <address>"));
    strUsage += HelpMessageOpt("-zmqpubrawtx=<address>", translate("Enable publish raw transaction in <address>"));
    strUsage += HelpMessageOpt("-zmqpubrawtxlock=<address>", translate("Enable publish raw transaction (locked via SwiftX) in <address>"));
#endif

    strUsage += HelpMessageGroup(translate("Debugging/Testing options:"));
    if (settings.GetBoolArg("-help-debug", false)) {
        strUsage += HelpMessageOpt("-checkblockindex", strprintf("Do a full consistency check for mapBlockIndex, setBlockIndexCandidates, chainActive and mapBlocksUnlinked occasionally. Also sets -checkmempool (default: %u)", Params(CBaseChainParams::MAIN).DefaultConsistencyChecks()));
        strUsage += HelpMessageOpt("-checkmempool=<n>", strprintf("Run checks every <n> transactions (default: %u)", Params(CBaseChainParams::MAIN).DefaultConsistencyChecks()));
        strUsage += HelpMessageOpt("-checkpoints", strprintf(translate("Only accept block chain matching built-in checkpoints (default: %u)"), 1));
        strUsage += HelpMessageOpt("-dblogsize=<n>", strprintf(translate("Flush database activity from memory pool to disk log every <n> megabytes (default: %u)"), 100));
        strUsage += HelpMessageOpt("-disablesafemode", strprintf(translate("Disable safemode, override a real safe mode event (default: %u)"), 0));
        strUsage += HelpMessageOpt("-testsafemode", strprintf(translate("Force safe mode (default: %u)"), 0));
        strUsage += HelpMessageOpt("-dropmessagestest=<n>", translate("Randomly drop 1 of every <n> network messages"));
        strUsage += HelpMessageOpt("-fuzzmessagestest=<n>", translate("Randomly fuzz 1 of every <n> network messages"));
        strUsage += HelpMessageOpt("-flushwallet", strprintf(translate("Run a thread to flush wallet periodically (default: %u)"), 1));
        strUsage += HelpMessageOpt("-maxreorg", strprintf(translate("Use a custom max chain reorganization depth (default: %u)"), 100));
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
    strUsage += HelpMessageOpt("-gen", strprintf(translate("Generate coins (default: %u)"), 0));
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
    strUsage += HelpMessageOpt("-minrelaytxfee=<amt>", strprintf(translate("Fees (in DIV/Kb) smaller than this are considered zero fee for relaying (default: %s)"), FormatMoney(::minRelayTxFee.GetFeePerK())));
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

//    strUsage += "  -anonymizediviamount=<n>     " + strprintf(translate("Keep N DIV anonymized (default: %u)"), 0) + "\n";
//    strUsage += "  -liquidityprovider=<n>       " + strprintf(translate("Provide liquidity to Obfuscation by infrequently mixing coins on a continual basis (0-100, default: %u, 1=very frequent, high fees, 100=very infrequent, low fees)"), 0) + "\n";

    strUsage += HelpMessageGroup(translate("SwiftX options:"));
    strUsage += HelpMessageOpt("-enableswifttx=<n>", strprintf(translate("Enable SwiftX, show confirmations for locked transactions (bool, default: %s)"), "true"));
    strUsage += HelpMessageOpt("-swifttxdepth=<n>", strprintf(translate("Show N confirmations for a successfully locked transaction (0-9999, default: %u)"), nSwiftTXDepth));

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

static void BlockNotifyCallback(const uint256& hashNewTip)
{
    std::string strCmd = settings.GetArg("-blocknotify", "");

    boost::replace_all(strCmd, "%s", hashNewTip.GetHex());
    boost::thread t(runCommand, strCmd); // thread runs free
}

struct CImportingNow {
    CImportingNow()
    {
        assert(fImporting == false);
        fImporting = true;
    }

    ~CImportingNow()
    {
        assert(fImporting == true);
        fImporting = false;
    }
};

void ThreadImport(std::vector<boost::filesystem::path> vImportFiles)
{
    RenameThread("divi-loadblk");

    // -reindex
    if (fReindex) {
        CImportingNow imp;
        int nFile = 0;
        while (true) {
            CDiskBlockPos pos(nFile, 0);
            if (!BlockFileExists(pos, "blk"))
                break; // No block files left to reindex
            FILE* file = OpenBlockFile(pos, true);
            if (!file)
                break; // This error is logged in OpenBlockFile
            LogPrintf("Reindexing block file blk%05u.dat...\n", (unsigned int)nFile);
            LoadExternalBlockFile(file, &pos);
            nFile++;
        }
        pblocktree->WriteReindexing(false);
        fReindex = false;
        LogPrintf("Reindexing finished\n");
        // To avoid ending up in a situation without genesis block, re-try initializing (no-op if reindexing worked):
        InitBlockIndex();
    }

    // hardcoded $DATADIR/bootstrap.dat
    boost::filesystem::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (boost::filesystem::exists(pathBootstrap)) {
        FILE* file = fopen(pathBootstrap.string().c_str(), "rb");
        if (file) {
            CImportingNow imp;
            boost::filesystem::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            LogPrintf("Importing bootstrap.dat...\n");
            LoadExternalBlockFile(file);
            RenameOver(pathBootstrap, pathBootstrapOld);
        } else {
            LogPrintf("Warning: Could not open bootstrap file %s\n", pathBootstrap.string());
        }
    }

    // -loadblock=
    BOOST_FOREACH (boost::filesystem::path& path, vImportFiles) {
        FILE* file = fopen(path.string().c_str(), "rb");
        if (file) {
            CImportingNow imp;
            LogPrintf("Importing blocks file %s...\n", path.string());
            LoadExternalBlockFile(file);
        } else {
            LogPrintf("Warning: Could not open blocks file %s\n", path.string());
        }
    }

    if (settings.GetBoolArg("-stopafterblockimport", false)) {
        LogPrintf("Stopping after block import\n");
        StartShutdown();
    }
}


/** Sanity checks
 *  Ensure that DIVI is running in a usable environment with all
 *  necessary library support.
 */
bool VerifyECCAndLibCCompatibilityAreAvailable(void)
{
    if (!ECC_InitSanityCheck()) {
        InitError("OpenSSL appears to lack support for elliptic curve cryptography. For more "
                  "information, visit https://en.bitcoin.it/wiki/OpenSSL_and_EC_Libraries");
        return false;
    }
    if (!glibc_sanity_test() || !glibcxx_sanity_test())
        return false;

    return true;
}

bool VerifyCriticalDependenciesAreAvailable()
{
    // ********************************************************* Step 4: sanity checks

    // Sanity check
    if (!VerifyECCAndLibCCompatibilityAreAvailable())
        return InitError(strprintf(translate("Initialization sanity check failed. %s is shutting down."), translate(PACKAGE_NAME)));

    // Probe the data directory lock to give an early error message, if possible
    return true;
}

void SetNetworkingParameters()
{
    if (settings.ParameterIsSet("-bind") || settings.ParameterIsSet("-whitebind")) {
        // when specifying an explicit binding address, you want to listen on it
        // even when -connect or -proxy is specified
        if (SoftSetBoolArg("-listen", true))
            LogPrintf("InitializeDivi : parameter interaction: -bind or -whitebind set -> setting -listen=1\n");
    }

    if (settings.ParameterIsSet("-connect") && mapMultiArgs["-connect"].size() > 0) {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        if (SoftSetBoolArg("-dnsseed", false))
            LogPrintf("InitializeDivi : parameter interaction: -connect set -> setting -dnsseed=0\n");
        if (SoftSetBoolArg("-listen", false))
            LogPrintf("InitializeDivi : parameter interaction: -connect set -> setting -listen=0\n");
    }

    if (settings.ParameterIsSet("-proxy")) {
        // to protect privacy, do not listen by default if a default proxy server is specified
        if (SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -listen=0\n", __func__);
        // to protect privacy, do not use UPNP when a proxy is set. The user may still specify -listen=1
        // to listen locally, so don't rely on this happening through -listen below.
        if (SoftSetBoolArg("-upnp", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -upnp=0\n", __func__);
        // to protect privacy, do not discover addresses by default
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("InitializeDivi : parameter interaction: -proxy set -> setting -discover=0\n");
    }

    if (!settings.GetBoolArg("-listen", true)) {
        // do not map ports or try to retrieve public IP when not listening (pointless)
        if (SoftSetBoolArg("-upnp", false))
            LogPrintf("InitializeDivi : parameter interaction: -listen=0 -> setting -upnp=0\n");
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("InitializeDivi : parameter interaction: -listen=0 -> setting -discover=0\n");
        if (SoftSetBoolArg("-listenonion", false))
            LogPrintf("InitializeDivi : parameter interaction: -listen=0 -> setting -listenonion=0\n");
    }

    if (settings.ParameterIsSet("-externalip")) {
        // if an explicit public IP is specified, do not try to find others
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("InitializeDivi : parameter interaction: -externalip set -> setting -discover=0\n");
    }

    nConnectTimeout = settings.GetArg("-timeout", DEFAULT_CONNECT_TIMEOUT);
    if (nConnectTimeout <= 0)
        nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;

    fAlerts = settings.GetBoolArg("-alerts", DEFAULT_ALERTS);
    if (settings.GetBoolArg("-peerbloomfilters", DEFAULT_PEERBLOOMFILTERS))
        nLocalServices |= NODE_BLOOM;
}

bool EnableWalletFeatures()
{
    if (settings.GetBoolArg("-salvagewallet", false)) {
        // Rewrite just private keys: rescan to find transactions
        if (SoftSetBoolArg("-rescan", true))
            LogPrintf("InitializeDivi : parameter interaction: -salvagewallet=1 -> setting -rescan=1\n");
    }

    // -zapwallettx implies a rescan
    if (settings.GetBoolArg("-zapwallettxes", false)) {
        if (SoftSetBoolArg("-rescan", true))
            LogPrintf("InitializeDivi : parameter interaction: -zapwallettxes=<mode> -> setting -rescan=1\n");
    }

    if (!settings.GetBoolArg("-enableswifttx", fEnableSwiftTX)) {
        if (SoftSetArg("-swifttxdepth", 0))
            LogPrintf("InitializeDivi : parameter interaction: -enableswifttx=false -> setting -nSwiftTXDepth=0\n");
    }

    return true;
}

bool SetMaxConnectionsAndFileDescriptors(int& nFD)
{
    int nBind = std::max((int)settings.ParameterIsSet("-bind") + (int)settings.ParameterIsSet("-whitebind"), 1);
    nMaxConnections = settings.GetArg("-maxconnections", 125);
    nMaxConnections = std::max(std::min(nMaxConnections, (int)(FD_SETSIZE - nBind - MIN_CORE_FILEDESCRIPTORS)), 0);
    nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS);
    if (nFD < MIN_CORE_FILEDESCRIPTORS)
        return InitError(translate("Not enough file descriptors available."));
    if (nFD - MIN_CORE_FILEDESCRIPTORS < nMaxConnections)
        nMaxConnections = nFD - MIN_CORE_FILEDESCRIPTORS;

    return true;
}

bool CheckCriticalUnsupportedFeaturesAreNotUsed()
{
    // Check for -debugnet
    if (settings.GetBoolArg("-debugnet", false))
        InitWarning(translate("Warning: Unsupported argument -debugnet ignored, use -debug=net."));
    // Check for -socks - as this is a privacy risk to continue, exit here
    if (settings.ParameterIsSet("-socks"))
        return InitError(translate("Error: Unsupported argument -socks found. Setting SOCKS version isn't possible anymore, only SOCKS5 proxies are supported."));
    // Check for -tor - as this is a privacy risk to continue, exit here
    if (settings.GetBoolArg("-tor", false))
        return InitError(translate("Error: Unsupported argument -tor found, use -onion."));
    // Check level must be 4 for zerocoin checks
    if (settings.ParameterIsSet("-checklevel"))
        return InitError(translate("Error: Unsupported argument -checklevel found. Checklevel must be level 4."));

    if (settings.GetBoolArg("-benchmark", false))
        InitWarning(translate("Warning: Unsupported argument -benchmark ignored, use -debug=bench."));

    return true;
}

void SetConsistencyChecks()
{
    // Checkmempool and checkblockindex default to true in regtest mode
    mempool.setSanityCheck(settings.GetBoolArg("-checkmempool", Params().DefaultConsistencyChecks()));
    fCheckBlockIndex = settings.GetBoolArg("-checkblockindex", Params().DefaultConsistencyChecks());
    CCheckpointServices::fEnabled = settings.GetBoolArg("-checkpoints", true);
}

void SetNumberOfThreadsToCheckScripts()
{
    // -par=0 means autodetect, but nScriptCheckThreads==0 means no concurrency
    nScriptCheckThreads = settings.GetArg("-par", DEFAULT_SCRIPTCHECK_THREADS);
    if (nScriptCheckThreads <= 0)
        nScriptCheckThreads += boost::thread::hardware_concurrency();
    if (nScriptCheckThreads <= 1)
        nScriptCheckThreads = 0;
    else if (nScriptCheckThreads > MAX_SCRIPTCHECK_THREADS)
        nScriptCheckThreads = MAX_SCRIPTCHECK_THREADS;
}

bool WalletIsDisabled()
{
#ifdef ENABLE_WALLET
    return settings.GetBoolArg("-disablewallet", false);
#else
    return true;
#endif
}

bool SetTransactionRequirements()
{
    // Fee-per-kilobyte amount considered the same as "free"
    // If you are mining, be careful setting this:
    // if you set it to zero then
    // a transaction spammer can cheaply fill blocks using
    // 1-satoshi-fee transactions. It should be set above the real
    // cost to you of processing a transaction.
    if (settings.ParameterIsSet("-minrelaytxfee")) {
        CAmount n = 0;
        if (ParseMoney(settings.GetParameter("-minrelaytxfee"), n) && n > 0)
            ::minRelayTxFee = CFeeRate(n);
        else
            return InitError(strprintf(translate("Invalid amount for -minrelaytxfee=<amount>: '%s'"), settings.GetParameter("-minrelaytxfee")));
    }
#ifdef ENABLE_WALLET
    if (settings.ParameterIsSet("-mintxfee")) {
        CAmount n = 0;
        if (ParseMoney(settings.GetParameter("-mintxfee"), n) && n > 0)
            CWallet::minTxFee = CFeeRate(n);
        else
            return InitError(strprintf(translate("Invalid amount for -mintxfee=<amount>: '%s'"), settings.GetParameter("-mintxfee")));
    }
    if (settings.ParameterIsSet("-paytxfee")) {
        CAmount nFeePerK = 0;
        if (!ParseMoney(settings.GetParameter("-paytxfee"), nFeePerK))
            return InitError(strprintf(translate("Invalid amount for -paytxfee=<amount>: '%s'"), settings.GetParameter("-paytxfee")));
        if (nFeePerK > nHighTransactionFeeWarning)
            InitWarning(translate("Warning: -paytxfee is set very high! This is the transaction fee you will pay if you send a transaction."));
        payTxFee = CFeeRate(nFeePerK, 1000);
        if (payTxFee < ::minRelayTxFee) {
            return InitError(strprintf(translate("Invalid amount for -paytxfee=<amount>: '%s' (must be at least %s)"),
                settings.GetParameter("-paytxfee"), ::minRelayTxFee.ToString()));
        }
    }
    if (settings.ParameterIsSet("-maxtxfee")) {
        CAmount nMaxFee = 0;
        if (!ParseMoney(settings.GetParameter("-maxtxfee"), nMaxFee))
            return InitError(strprintf(translate("Invalid amount for -maxtxfee=<amount>: '%s'"), settings.GetParameter("-maxtxfee")));
        if (nMaxFee > nHighTransactionMaxFeeWarning)
            InitWarning(translate("Warning: -maxtxfee is set very high! Fees this large could be paid on a single transaction."));
        maxTxFee = nMaxFee;
        if (CFeeRate(maxTxFee, 1000) < ::minRelayTxFee) {
            return InitError(strprintf(translate("Invalid amount for -maxtxfee=<amount>: '%s' (must be at least the minrelay fee of %s to prevent stuck transactions)"),
                settings.GetParameter("-maxtxfee"), ::minRelayTxFee.ToString()));
        }
    }
    nTxConfirmTarget = settings.GetArg("-txconfirmtarget", 1);
    bdisableSystemnotifications = settings.GetBoolArg("-disablesystemnotifications", false);
    fSendFreeTransactions = settings.GetBoolArg("-sendfreetransactions", false);
#endif
    fIsBareMultisigStd = settings.GetBoolArg("-permitbaremultisig", true) != 0;
    nMaxDatacarrierBytes = settings.GetArg("-datacarriersize", nMaxDatacarrierBytes);
    return true;
}

void SetLoggingAndDebugSettings()
{
    fPrintToConsole = settings.GetBoolArg("-printtoconsole", false);
    fLogTimestamps = settings.GetBoolArg("-logtimestamps", true);
    fLogIPs = settings.GetBoolArg("-logips", false);

    fDebug = !mapMultiArgs["-debug"].empty();
    // Special-case: if -debug=0/-nodebug is set, turn off debugging messages
    const std::vector<std::string>& categories = mapMultiArgs["-debug"];
    if (settings.GetBoolArg("-nodebug", false) || std::find(categories.begin(), categories.end(), std::string("0")) != categories.end())
        fDebug = false;

    if (settings.GetBoolArg("-shrinkdebugfile", !fDebug))
        ShrinkDebugFile();

    if(fPrintToConsole)
    {
        setvbuf(stdout, NULL, _IOLBF, 0);
    }
}

bool TryLockDataDirectory(const std::string& datadir)
{
    // Make sure only a single DIVI process is using the data directory.
    boost::filesystem::path pathLockFile = GetDataDir() / ".lock";
    FILE* file = fopen(pathLockFile.string().c_str(), "a"); // empty lock file; created if it doesn't exist.
    if (file) fclose(file);
    static boost::interprocess::file_lock lock(pathLockFile.string().c_str());

    // Wait maximum 10 seconds if an old wallet is still running. Avoids lockup during restart
    if (!lock.timed_lock(boost::get_system_time() + boost::posix_time::seconds(10)))
        return InitError(strprintf(translate("Cannot obtain a lock on data directory %s. DIVI Core is probably already running."), datadir));
    return true;
}

bool CheckWalletFileExists(std::string strDataDir)
{
    std::string strWalletFile = settings.GetArg("-wallet", "wallet.dat");
    // Wallet file must be a plain filename without a directory
    if (strWalletFile != boost::filesystem::basename(strWalletFile) + boost::filesystem::extension(strWalletFile))
        return InitError(strprintf(translate("Wallet %s resides outside data directory %s"), strWalletFile, strDataDir));

    return true;
}

void StartScriptVerificationThreads(boost::thread_group& threadGroup)
{
    if (nScriptCheckThreads) {
        for (int i = 0; i < nScriptCheckThreads - 1; i++)
            threadGroup.create_thread(&ThreadScriptCheck);
    }
}


#ifdef ENABLE_WALLET

void ClearFoldersForResync()
{
    uiInterface.InitMessage(translate("Preparing for resync..."));
    // Delete the local blockchain folders to force a resync from scratch to get a consitent blockchain-state
    boost::filesystem::path blocksDir = GetDataDir() / "blocks";
    boost::filesystem::path chainstateDir = GetDataDir() / "chainstate";
    boost::filesystem::path sporksDir = GetDataDir() / "sporks";

    LogPrintf("Deleting blockchain folders blocks, chainstate and sporks\n");
    // We delete in 4 individual steps in case one of the folder is missing already
    try {
        if (boost::filesystem::exists(blocksDir)){
            boost::filesystem::remove_all(blocksDir);
            LogPrintf("-resync: folder deleted: %s\n", blocksDir.string().c_str());
        }

        if (boost::filesystem::exists(chainstateDir)){
            boost::filesystem::remove_all(chainstateDir);
            LogPrintf("-resync: folder deleted: %s\n", chainstateDir.string().c_str());
        }

        if (boost::filesystem::exists(sporksDir)){
            boost::filesystem::remove_all(sporksDir);
            LogPrintf("-resync: folder deleted: %s\n", sporksDir.string().c_str());
        }

    } catch (boost::filesystem::filesystem_error& error) {
        LogPrintf("Failed to delete blockchain folders %s\n", error.what());
    }
}

#endif
bool BackupWallet(std::string strDataDir, bool fDisableWallet)
{
#ifdef ENABLE_WALLET
    std::string strWalletFile = settings.GetArg("-wallet", "wallet.dat");
    if (!fDisableWallet) {
        WalletBackupFeatureContainer walletBackupFeatureContainer(
            settings.GetArg("-createwalletbackups",nWalletBackups), strWalletFile, strDataDir);
        LogPrintf("backing up wallet\n");
        if(walletBackupFeatureContainer.GetWalletIntegrityVerifier().CheckWalletIntegrity(strDataDir, strWalletFile))
        {
            return walletBackupFeatureContainer.GetBackupCreator().BackupWallet() &&
                walletBackupFeatureContainer.GetMonthlyBackupCreator().BackupWallet();
        }
        else
        {
            LogPrintf("Error: Wallet integrity check failed.");
            return false;
        }
    }
#endif // ENABLE_WALLET
    return true;
}

bool InitializeP2PNetwork()
{
    RegisterNodeSignals(GetNodeSignals());

    if (settings.ParameterIsSet("-onlynet")) {
        std::set<enum Network> nets;
        BOOST_FOREACH (std::string snet, mapMultiArgs["-onlynet"]) {
            enum Network net = ParseNetwork(snet);
            if (net == NET_UNROUTABLE)
                return InitError(strprintf(translate("Unknown network specified in -onlynet: '%s'"), snet));
            nets.insert(net);
        }
        for (int n = 0; n < NET_MAX; n++) {
            enum Network net = (enum Network)n;
            if (!nets.count(net))
                SetLimited(net);
        }
    }

    if (settings.ParameterIsSet("-whitelist")) {
        BOOST_FOREACH (const std::string& net, mapMultiArgs["-whitelist"]) {
            CSubNet subnet(net);
            if (!subnet.IsValid())
                return InitError(strprintf(translate("Invalid netmask specified in -whitelist: '%s'"), net));
            CNode::AddWhitelistedRange(subnet);
        }
    }

    // Check for host lookup allowed before parsing any network related parameters
    fNameLookup = settings.GetBoolArg("-dns", DEFAULT_NAME_LOOKUP);

    bool proxyRandomize = settings.GetBoolArg("-proxyrandomize", true);
    // -proxy sets a proxy for all outgoing network traffic
    // -noproxy (or -proxy=0) as well as the empty string can be used to not set a proxy, this is the default
    std::string proxyArg = settings.GetArg("-proxy", "");
    if (proxyArg != "" && proxyArg != "0") {
        CService proxyAddr;
        if (!Lookup(proxyArg.c_str(), proxyAddr, 9050, fNameLookup)) {
            return InitError(strprintf(translate("Invalid -proxy address or hostname: '%s'"), proxyArg));
        }

        proxyType addrProxy = proxyType(proxyAddr, proxyRandomize);
        if (!addrProxy.IsValid())
            return InitError(strprintf(translate("Invalid -proxy address or hostname: '%s'"), proxyArg));

        SetProxy(NET_IPV4, addrProxy);
        SetProxy(NET_IPV6, addrProxy);
        SetProxy(NET_TOR, addrProxy);
        SetNameProxy(addrProxy);
        SetReachable(NET_TOR); // by default, -proxy sets onion as reachable, unless -noonion later
    }

    // -onion can be used to set only a proxy for .onion, or override normal proxy for .onion addresses
    // -noonion (or -onion=0) disables connecting to .onion entirely
    // An empty string is used to not override the onion proxy (in which case it defaults to -proxy set above, or none)
    std::string onionArg = settings.GetArg("-onion", "");
    if (onionArg != "") {
        if (onionArg == "0") { // Handle -noonion/-onion=0
            SetReachable(NET_TOR, false); // set onions as unreachable
        } else {
            CService onionProxy;
            if (!Lookup(onionArg.c_str(), onionProxy, 9050, fNameLookup)) {
                return InitError(strprintf(translate("Invalid -onion address or hostname: '%s'"), onionArg));
            }
            proxyType addrOnion = proxyType(onionProxy, proxyRandomize);
            if (!addrOnion.IsValid())
                return InitError(strprintf(translate("Invalid -onion address or hostname: '%s'"), onionArg));
            SetProxy(NET_TOR, addrOnion);
            SetReachable(NET_TOR);
        }
    }

    // see Step 2: parameter interactions for more information about these
    fListen = settings.GetBoolArg("-listen", DEFAULT_LISTEN);
    fDiscover = settings.GetBoolArg("-discover", true);

    bool fBound = false;
    if (fListen) {
        if (settings.ParameterIsSet("-bind") || settings.ParameterIsSet("-whitebind")) {
            BOOST_FOREACH (std::string strBind, mapMultiArgs["-bind"]) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false))
                    return InitError(strprintf(translate("Cannot resolve -bind address: '%s'"), strBind));
                fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR));
            }
            BOOST_FOREACH (std::string strBind, mapMultiArgs["-whitebind"]) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, 0, false))
                    return InitError(strprintf(translate("Cannot resolve -whitebind address: '%s'"), strBind));
                if (addrBind.GetPort() == 0)
                    return InitError(strprintf(translate("Need to specify a port with -whitebind: '%s'"), strBind));
                fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR | BF_WHITELIST));
            }
        } else {
            struct in_addr inaddr_any;
            inaddr_any.s_addr = INADDR_ANY;
            fBound |= Bind(CService(in6addr_any, GetListenPort()), BF_NONE);
            fBound |= Bind(CService(inaddr_any, GetListenPort()), !fBound ? BF_REPORT_ERROR : BF_NONE);
        }
        if (!fBound)
            return InitError(translate("Failed to listen on any port. Use -listen=0 if you want this."));
    }

    if (settings.ParameterIsSet("-externalip")) {
        BOOST_FOREACH (std::string strAddr, mapMultiArgs["-externalip"]) {
            CService addrLocal(strAddr, GetListenPort(), fNameLookup);
            if (!addrLocal.IsValid())
                return InitError(strprintf(translate("Cannot resolve -externalip address: '%s'"), strAddr));
            AddLocal(CService(strAddr, GetListenPort(), fNameLookup), LOCAL_MANUAL);
        }
    }

    BOOST_FOREACH (std::string strDest, mapMultiArgs["-seednode"])
        AddOneShot(strDest);

    return true;
}

void PruneHDSeedParameterInteraction()
{
    if (settings.ParameterIsSet("-hdseed") && IsHex(settings.GetArg("-hdseed", "not hex")) && (settings.ParameterIsSet("-mnemonic") || settings.ParameterIsSet("-mnemonicpassphrase"))) {
        settings.ForceRemoveArg("-mnemonic");
        settings.ForceRemoveArg("-mnemonicpassphrase");
        LogPrintf("%s: parameter interaction: can't use -hdseed and -mnemonic/-mnemonicpassphrase together, will prefer -seed\n", __func__);
    }
}

void PrintInitialLogHeader(bool fDisableWallet, int numberOfFileDescriptors, const std::string& dataDirectoryInUse)
{
    LogPrintf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    LogPrintf("DIVI version %s (%s)\n", FormatFullVersion(), CLIENT_DATE);
    LogPrintf("Using OpenSSL version %s\n", SSLeay_version(SSLEAY_VERSION));
    if(!fDisableWallet) LogPrintf("Using BerkeleyDB version %s\n", DbEnv::version(0, 0, 0));
    if (!fLogTimestamps) LogPrintf("Startup time: %s\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()));
    LogPrintf("Default data directory %s\n", GetDefaultDataDir().string());
    LogPrintf("Using data directory %s\n", dataDirectoryInUse);
    LogPrintf("Using config file %s\n", GetConfigFile().string());
    LogPrintf("Using at most %i connections (%i file descriptors available)\n", nMaxConnections, numberOfFileDescriptors);
    LogPrintf("Using %u threads for script verification\n", nScriptCheckThreads);
}

bool SetSporkKey()
{
    sporkManager.SetSporkAddress(Params().SporkKey());
    if (settings.ParameterIsSet("-sporkkey")) // spork priv key
    {
        if (!sporkManager.SetPrivKey(settings.GetArg("-sporkkey", "")))
            return InitError(translate("Unable to sign spork message, wrong key?"));
    }
    return true;
}

void WarmUpRPCAndStartRPCThreads()
{
    /* Start the RPC server already.  It will be started in "warmup" mode
     * and not really process calls already (but it will signify connections
     * that the server is there and will be ready later).  Warmup mode will
     * be disabled when initialisation is finished.
     */
    if (settings.GetBoolArg("-server", false)) {
        uiInterface.InitMessage.connect(SetRPCWarmupStatus);
        StartRPCThreads();
    }
}

void CreateHardlinksForBlocks()
{
    fReindex = settings.GetBoolArg("-reindex", false);
    // Upgrading to 0.8; hard-link the old blknnnn.dat files into /blocks/
    boost::filesystem::path blocksDir = GetDataDir() / "blocks";
    if (!boost::filesystem::exists(blocksDir)) {
        boost::filesystem::create_directories(blocksDir);
        bool linked = false;
        for (unsigned int i = 1; i < 10000; i++) {
            boost::filesystem::path source = GetDataDir() / strprintf("blk%04u.dat", i);
            if (!boost::filesystem::exists(source)) break;
            boost::filesystem::path dest = blocksDir / strprintf("blk%05u.dat", i - 1);
            try {
                boost::filesystem::create_hard_link(source, dest);
                LogPrintf("Hardlinked %s -> %s\n", source.string(), dest.string());
                linked = true;
            } catch (boost::filesystem::filesystem_error& e) {
                // Note: hardlink creation failing is not a disaster, it just means
                // blocks will get re-downloaded from peers.
                LogPrintf("Error hardlinking blk%04u.dat : %s\n", i, e.what());
                break;
            }
        }
        if (linked) {
            fReindex = true;
        }
    }
}

std::pair<size_t,size_t> CalculateDBCacheSizes()
{
    size_t nTotalCache = (settings.GetArg("-dbcache", nDefaultDbCache) << 20);
    size_t nBlockTreeDBCache = 0;
    size_t nCoinDBCache = 0;
    if (nTotalCache < (nMinDbCache << 20))
        nTotalCache = (nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    else if (nTotalCache > (nMaxDbCache << 20))
        nTotalCache = (nMaxDbCache << 20); // total cache cannot be greater than nMaxDbCache
    nBlockTreeDBCache = nTotalCache / 8;
    if (nBlockTreeDBCache > (1 << 21) && !settings.GetBoolArg("-txindex", true))
        nBlockTreeDBCache = (1 << 21); // block tree db cache shouldn't be larger than 2 MiB
    nTotalCache -= nBlockTreeDBCache;
    nCoinDBCache = nTotalCache / 2; // use half of the remaining cache for coindb cache
    nTotalCache -= nCoinDBCache;
    nCoinCacheSize = nTotalCache / 300; // coins in memory require around 300 bytes

    return std::make_pair(nBlockTreeDBCache,nCoinDBCache);
}

bool TryToLoadBlocks(bool& fLoaded, std::string& strLoadError)
{
    const bool skipLoadingDueToError = true;
    try {
        UnloadBlockIndex();
        std::pair<std::size_t, std::size_t> dbCacheSizes = CalculateDBCacheSizes();
        CleanAndReallocateShallowDatabases(dbCacheSizes);

        if (fReindex)
            pblocktree->WriteReindexing(true);

        // DIVI: load previous sessions sporks if we have them.
        uiInterface.InitMessage(translate("Loading sporks..."));
        sporkManager.LoadSporksFromDB();

        uiInterface.InitMessage(translate("Loading block index..."));
        std::string strBlockIndexError = "";
        if (!LoadBlockIndex(strBlockIndexError)) {
            strLoadError = translate("Error loading block database");
            strLoadError = strprintf("%s : %s", strLoadError, strBlockIndexError);
            return skipLoadingDueToError;
        }

        // If the loaded chain has a wrong genesis, bail out immediately
        // (we're likely using a testnet datadir, or the other way around).
        if (!mapBlockIndex.empty() && mapBlockIndex.count(Params().HashGenesisBlock()) == 0)
            return InitError(translate("Incorrect or no genesis block found. Wrong datadir for network?"));

        // Initialize the block index (no-op if non-empty database was already loaded)
        if (!InitBlockIndex()) {
            strLoadError = translate("Error initializing block database");
            return skipLoadingDueToError;
        }

        // Check for changed -txindex state
        if (fTxIndex != settings.GetBoolArg("-txindex", true)) {
            strLoadError = translate("You need to rebuild the database using -reindex to change -txindex");
            return skipLoadingDueToError;
        }

        uiInterface.InitMessage(translate("Verifying blocks..."));

        // Flag sent to validation code to let it know it can skip certain checks
        fVerifyingBlocks = true;

        if (!CVerifyDB().VerifyDB(pcoinsdbview, 4, settings.GetArg("-checkblocks", 100))) {
            strLoadError = translate("Corrupted block database detected");
            fVerifyingBlocks = false;
            return skipLoadingDueToError;
        }
    } catch (std::exception& e) {
        if (fDebug) LogPrintf("%s\n", e.what());
        strLoadError = translate("Error opening block database");
        fVerifyingBlocks = false;
        return skipLoadingDueToError;
    }

    fVerifyingBlocks = false;
    fLoaded = true;

    return true;
}

bool CreateNewWalletIfOneIsNotAvailable(std::string strWalletFile, std::ostringstream& strErrors)
{
    bool fFirstRun = true;
    pwalletMain = new CWallet(strWalletFile);
    DBErrors nLoadWalletRet = pwalletMain->LoadWallet(fFirstRun);
    if (nLoadWalletRet != DB_LOAD_OK) {
        if (nLoadWalletRet == DB_CORRUPT)
            strErrors << translate("Error loading wallet.dat: Wallet corrupted") << "\n";
        else if (nLoadWalletRet == DB_NONCRITICAL_ERROR) {
            std::string msg(translate("Warning: error reading wallet.dat! All keys read correctly, but transaction data"
                            " or address book entries might be missing or incorrect."));
            InitWarning(msg);
        } else if (nLoadWalletRet == DB_TOO_NEW)
            strErrors << translate("Error loading wallet.dat: Wallet requires newer version of DIVI Core") << "\n";
        else if (nLoadWalletRet == DB_NEED_REWRITE) {
            strErrors << translate("Wallet needed to be rewritten: restart DIVI Core to complete") << "\n";
            LogPrintf("%s", strErrors.str());
            return InitError(strErrors.str());
        } else
            strErrors << translate("Error loading wallet.dat") << "\n";
    }

    if (settings.GetBoolArg("-upgradewallet", fFirstRun)) {
        int nMaxVersion = settings.GetArg("-upgradewallet", 0);
        if (nMaxVersion == 0) // the -upgradewallet without argument case
        {
            LogPrintf("Performing wallet upgrade to %i\n", FEATURE_LATEST);
            nMaxVersion = CLIENT_VERSION;
            pwalletMain->SetMinVersion(FEATURE_LATEST); // permanently upgrade the wallet immediately
        } else
            LogPrintf("Allowing wallet upgrade up to %i\n", nMaxVersion);
        if (nMaxVersion < pwalletMain->GetVersion())
            strErrors << translate("Cannot downgrade wallet") << "\n";
        pwalletMain->SetMaxVersion(nMaxVersion);
    }

    if (fFirstRun)
    {
        // Create new keyUser and set as default key
        if (settings.GetBoolArg("-usehd", DEFAULT_USE_HD_WALLET) && !pwalletMain->IsHDEnabled()) {
            if (settings.GetArg("-mnemonicpassphrase", "").size() > 256) {
                InitError(translate("Mnemonic passphrase is too long, must be at most 256 characters"));
                return false;
            }
            // generate a new master key
            pwalletMain->GenerateNewHDChain();

            // ensure this wallet.dat can only be opened by clients supporting HD
            pwalletMain->SetMinVersion(FEATURE_HD);
        }

        CPubKey newDefaultKey;
        if (pwalletMain->GetKeyFromPool(newDefaultKey, false)) {
            pwalletMain->SetDefaultKey(newDefaultKey);
            if (!pwalletMain->SetAddressBook(pwalletMain->vchDefaultKey.GetID(), "", "receive")) {
                InitError(translate("Cannot write default address") += "\n");
                return false;
            }
        }

        pwalletMain->SetBestChain(chainActive.GetLocator());

    }
    else if (settings.ParameterIsSet("-usehd")) {
        bool useHD = settings.GetBoolArg("-usehd", DEFAULT_USE_HD_WALLET);
        if (pwalletMain->IsHDEnabled() && !useHD) {
            InitError(strprintf(translate("Error loading %s: You can't disable HD on a already existing HD wallet"),
                                pwalletMain->strWalletFile));
            return false;
        }
        if (!pwalletMain->IsHDEnabled() && useHD) {
            InitError(strprintf(translate("Error loading %s: You can't enable HD on a already existing non-HD wallet"),
                                pwalletMain->strWalletFile));
            return false;
        }
    }

    // Warn user every time he starts non-encrypted HD wallet
    if (!settings.GetBoolArg("-allowunencryptedwallet", false) && settings.GetBoolArg("-usehd", DEFAULT_USE_HD_WALLET) && !pwalletMain->IsLocked()) {
        InitWarning(translate("Make sure to encrypt your wallet and delete all non-encrypted backups after you verified that wallet works!"));
    }

    return true;
}

void ScanBlockchainForWalletUpdates(std::string strWalletFile,const std::vector<CWalletTx>& vWtx, int64_t& nStart)
{
    CBlockIndex* pindexRescan = chainActive.Tip();
    if (settings.GetBoolArg("-rescan", false))
        pindexRescan = chainActive.Genesis();
    else {
        CWalletDB walletdb(strWalletFile);
        CBlockLocator locator;
        if (walletdb.ReadBestBlock(locator))
            pindexRescan = FindForkInGlobalIndex(chainActive, locator);
        else
            pindexRescan = chainActive.Genesis();
    }
    if (chainActive.Tip() && chainActive.Tip() != pindexRescan) {
        uiInterface.InitMessage(translate("Rescanning..."));
        LogPrintf("Rescanning last %i blocks (from block %i)...\n", chainActive.Height() - pindexRescan->nHeight, pindexRescan->nHeight);
        nStart = GetTimeMillis();
        pwalletMain->ScanForWalletTransactions(pindexRescan, true);
        LogPrintf(" rescan      %15dms\n", GetTimeMillis() - nStart);
        pwalletMain->SetBestChain(chainActive.GetLocator());
        pwalletMain->IncrementDBUpdateCount();

        // Restore wallet transaction metadata after -zapwallettxes=1
        if (settings.GetBoolArg("-zapwallettxes", false) && settings.GetArg("-zapwallettxes", "1") != "2") {
            pwalletMain->UpdateTransactionMetadata(vWtx);
        }
    }
}

bool InitializeDivi(boost::thread_group& threadGroup)
{
// ********************************************************* Step 1: setup
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0));
#endif
#if _MSC_VER >= 1400
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
// Enable Data Execution Prevention (DEP)
// Minimum supported OS versions: WinXP SP3, WinVista >= SP1, Win Server 2008
// A failure is non-critical and needs no further attention!
#ifndef PROCESS_DEP_ENABLE
// We define this here, because GCCs winbase.h limits this to _WIN32_WINNT >= 0x0601 (Windows 7),
// which is not correct. Can be removed, when GCCs winbase.h is fixed!
#define PROCESS_DEP_ENABLE 0x00000001
#endif
    typedef BOOL(WINAPI * PSETPROCDEPPOL)(DWORD);
    PSETPROCDEPPOL setProcDEPPol = (PSETPROCDEPPOL)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetProcessDEPPolicy");
    if (setProcDEPPol != NULL) setProcDEPPol(PROCESS_DEP_ENABLE);

    // Initialize Windows Sockets
    WSADATA wsadata;
    int ret = WSAStartup(MAKEWORD(2, 2), &wsadata);
    if (ret != NO_ERROR || LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wVersion) != 2) {
        return InitError(strprintf("Error: Winsock library failed to start (WSAStartup returned error %d)", ret));
    }
#endif
#ifndef WIN32

    if (settings.GetBoolArg("-sysperms", false)) {
#ifdef ENABLE_WALLET
        if (!settings.GetBoolArg("-disablewallet", false))
            return InitError("Error: -sysperms is not allowed in combination with enabled wallet functionality");
#endif
    } else {
        umask(077);
    }


    // Clean shutdown on SIGTERM
    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // Reopen debug.log on SIGHUP
    struct sigaction sa_hup;
    sa_hup.sa_handler = HandleSIGHUP;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;
    sigaction(SIGHUP, &sa_hup, NULL);

#if defined(__SVR4) && defined(__sun)
    // ignore SIGPIPE on Solaris
    signal(SIGPIPE, SIG_IGN);
#endif
#endif

    // ********************************************************* Step 2: parameter interactions
    // Set this early so that parameter interactions go to console
    SetLoggingAndDebugSettings();

    SetNetworkingParameters();

    if(!EnableWalletFeatures())
    {
        return false;
    }

    // Make sure enough file descriptors are available
    int numberOfFileDescriptors;
    if(!SetMaxConnectionsAndFileDescriptors(numberOfFileDescriptors))
    {
        return false;
    }

    // ********************************************************* Step 3: parameter-to-internal-flags

    if(!CheckCriticalUnsupportedFeaturesAreNotUsed())
    {
        return false;
    }
    SetConsistencyChecks();
    SetNumberOfThreadsToCheckScripts();

    // Staking needs a CWallet instance, so make sure wallet is enabled
    bool fDisableWallet = WalletIsDisabled();
    if (fDisableWallet) {
        if (SoftSetBoolArg("-staking", false))
            LogPrintf("InitializeDivi : parameter interaction: wallet functionality not enabled -> setting -staking=0\n");
    }
    if(!SetTransactionRequirements())
    {
        return false;
    }
    // ********************************************************* Step 4: application initialization: dir lock, daemonize, pidfile, debug log
    // Sanity check
    if (!VerifyCriticalDependenciesAreAvailable())
        return InitError(translate("Initialization sanity check failed. DIVI Core is shutting down."));

    std::string strDataDir = GetDataDir().string();
    if(!TryLockDataDirectory(strDataDir))
    {
        return false;
    }
    if(!fDisableWallet && !CheckWalletFileExists(strDataDir))
    {
        return false;
    }

#ifndef WIN32
    CreatePidFile(GetPidFile(), getpid());
#endif
    PrintInitialLogHeader(fDisableWallet,numberOfFileDescriptors,strDataDir);
    StartScriptVerificationThreads(threadGroup);

    if(!SetSporkKey())
    {
        return false;
    }
    WarmUpRPCAndStartRPCThreads();

    int64_t nStart;

    // ********************************************************* Step 5: Backup wallet and verify wallet database integrity
    BackupWallet(strDataDir, fDisableWallet);
    if (settings.GetBoolArg("-resync", false))
    {
        ClearFoldersForResync();
    }
    // ********************************************************* Step 6: network initialization

    if(!InitializeP2PNetwork())
    {
        return false;
    }

    PruneHDSeedParameterInteraction();

#if ENABLE_ZMQ
    pzmqNotificationInterface = CZMQNotificationInterface::CreateWithArguments(settings);

    if (pzmqNotificationInterface) {
        RegisterValidationInterface(pzmqNotificationInterface);
    }
#endif

    // ********************************************************* Step 7: load block chain
    CreateHardlinksForBlocks();
    bool fLoaded = false;
    while (!fLoaded) {
        bool fReset = fReindex;
        std::string strLoadError;

        uiInterface.InitMessage(translate("Loading block index..."));

        nStart = GetTimeMillis();
        if(!TryToLoadBlocks(fLoaded,strLoadError))
        {
            return false;
        }

        if (!fLoaded) {
            // first suggest a reindex
            if (!fReset) {
                bool fRet = uiInterface.ThreadSafeMessageBox(
                    strLoadError + ".\n\n" + translate("Do you want to rebuild the block database now?"),
                    "", CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
                if (fRet) {
                    fReindex = true;
                    fRequestShutdown = false;
                } else {
                    LogPrintf("Aborted block database rebuild. Exiting.\n");
                    return false;
                }
            } else {
                return InitError(strLoadError);
            }
        }
    }

    // As LoadBlockIndex can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    // As the program has not fully started yet, Shutdown() is possibly overkill.
    if (fRequestShutdown) {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }
    LogPrintf(" block index %15dms\n", GetTimeMillis() - nStart);

    boost::filesystem::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
    CAutoFile est_filein(fopen(est_path.string().c_str(), "rb"), SER_DISK, CLIENT_VERSION);
    // Allowed to fail as this file IS missing on first startup.
    if (!est_filein.IsNull())
        mempool.ReadFeeEstimates(est_filein);
    fFeeEstimatesInitialized = true;

// ********************************************************* Step 8: load wallet
    std::ostringstream strErrors;
#ifdef ENABLE_WALLET
    std::string strWalletFile = settings.GetArg("-wallet", "wallet.dat");
    if (fDisableWallet) {
        pwalletMain = NULL;
        LogPrintf("Wallet disabled!\n");
    } else {
        // needed to restore wallet transaction meta data after -zapwallettxes
        std::vector<CWalletTx> vWtx;

        if (settings.GetBoolArg("-zapwallettxes", false)) {
            uiInterface.InitMessage(translate("Zapping all transactions from wallet..."));

            pwalletMain = new CWallet(strWalletFile);
            DBErrors nZapWalletRet = pwalletMain->ZapWalletTx(vWtx);
            if (nZapWalletRet != DB_LOAD_OK) {
                uiInterface.InitMessage(translate("Error loading wallet.dat: Wallet corrupted"));
                return false;
            }

            delete pwalletMain;
            pwalletMain = NULL;
        }

        uiInterface.InitMessage(translate("Loading wallet..."));
        fVerifyingBlocks = true;

        nStart = GetTimeMillis();
        if(!CreateNewWalletIfOneIsNotAvailable(strWalletFile,strErrors))
        {
            return false;
        }
        pwalletMain->toggleSpendingZeroConfirmationOutputs(
            settings.GetBoolArg("-spendzeroconfchange", false)
        );


        LogPrintf("%s", strErrors.str());
        LogPrintf(" wallet      %15dms\n", GetTimeMillis() - nStart);

        RegisterValidationInterface(pwalletMain);

        ScanBlockchainForWalletUpdates(strWalletFile,vWtx,nStart);
        fVerifyingBlocks = false;

    }  // (!fDisableWallet)
#else  // ENABLE_WALLET
    LogPrintf("No wallet compiled in!\n");
#endif // !ENABLE_WALLET
    // ********************************************************* Step 9: import blocks

    if (settings.ParameterIsSet("-blocknotify"))
        uiInterface.NotifyBlockTip.connect(BlockNotifyCallback);

    // scan for better chains in the block chain database, that are not yet connected in the active best chain
    CValidationState state;
    if (!ActivateBestChain(state))
        strErrors << "Failed to connect best block";

    std::vector<boost::filesystem::path> vImportFiles;
    if (settings.ParameterIsSet("-loadblock")) {
        BOOST_FOREACH (std::string strFile, mapMultiArgs["-loadblock"])
            vImportFiles.push_back(strFile);
    }
    threadGroup.create_thread(boost::bind(&ThreadImport, vImportFiles));
    if (chainActive.Tip() == NULL) {
        LogPrintf("Waiting for genesis block to be imported...\n");
        while (!fRequestShutdown && chainActive.Tip() == NULL)
            MilliSleep(10);
    }

    // ********************************************************* Step 10: setup ObfuScation

    if(!LoadDataCaches())
    {
        return false;
    }


    fMasterNode = settings.GetBoolArg("-masternode", false);

    if ((fMasterNode || masternodeConfig.getCount() > -1) && fTxIndex == false) {
        return InitError("Enabling Masternode support requires turning on transaction indexing."
                         "Please add txindex=1 to your configuration and start with -reindex");
    }

    if (fMasterNode) {
        LogPrintf("IS MASTER NODE\n");
        strMasterNodeAddr = settings.GetArg("-masternodeaddr", "");

        LogPrintf(" addr %s\n", strMasterNodeAddr.c_str());

        if (!strMasterNodeAddr.empty()) {
            CService addrTest = CService(strMasterNodeAddr);
            if (!addrTest.IsValid()) {
                return InitError("Invalid -masternodeaddr address: " + strMasterNodeAddr);
            }
        }

        strMasterNodePrivKey = settings.GetArg("-masternodeprivkey", "");
        if (!strMasterNodePrivKey.empty()) {
            std::string errorMessage;

            CKey key;
            CPubKey pubkey;

            if (!CObfuScationSigner::SetKey(strMasterNodePrivKey, errorMessage, key, pubkey)) {
                return InitError(translate("Invalid masternodeprivkey. Please see documenation."));
            }

            activeMasternode.pubKeyMasternode = pubkey;

        } else {
            return InitError(translate("You must specify a masternodeprivkey in the configuration. Please see documentation for help."));
        }
    }

    if (settings.GetBoolArg("-mnconflock", true) && pwalletMain) {
        LOCK(pwalletMain->cs_wallet);
        LogPrintf("Locking Masternodes:\n");
        uint256 mnTxHash;
        BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
            LogPrintf("  %s %s\n", mne.getTxHash(), mne.getOutputIndex());
            mnTxHash.SetHex(mne.getTxHash());
            COutPoint outpoint = COutPoint(mnTxHash, boost::lexical_cast<unsigned int>(mne.getOutputIndex()));
            pwalletMain->LockCoin(outpoint);
        }
    }

// XX42 Remove/refactor code below. Until then provide safe defaults
    nAnonymizeDiviAmount = 2;

    fEnableSwiftTX = false; //settings.GetBoolArg("-enableswifttx", fEnableSwiftTX);
    nSwiftTXDepth = settings.GetArg("-swifttxdepth", nSwiftTXDepth);
    nSwiftTXDepth = std::min(std::max(nSwiftTXDepth, 0), 60);

    //lite mode disables all Masternode and Obfuscation related functionality
    fLiteMode = settings.GetBoolArg("-litemode", false);
    if (fMasterNode && fLiteMode) {
        return InitError("You can not start a masternode in litemode");
    }

    LogPrintf("fLiteMode %d\n", fLiteMode);
    LogPrintf("nSwiftTXDepth %d\n", nSwiftTXDepth);
    LogPrintf("Anonymize DIVI Amount %d\n", nAnonymizeDiviAmount);

    threadGroup.create_thread(boost::bind(&ThreadCheckObfuScationPool));

    // ********************************************************* Step 11: start node

    if (!CheckDiskSpace())
        return false;

    if (!strErrors.str().empty())
        return InitError(strErrors.str());

    //// debug print
    LogPrintf("mapBlockIndex.size() = %u\n", mapBlockIndex.size());
    LogPrintf("chainActive.Height() = %d\n", chainActive.Height());
#ifdef ENABLE_WALLET
    LogPrintf("setKeyPool.size() = %u\n", pwalletMain ? pwalletMain->GetKeyPoolSize() : 0);
    LogPrintf("mapAddressBook.size() = %u\n", pwalletMain ? pwalletMain->mapAddressBook.size() : 0);
#endif

    if (settings.GetBoolArg("-listenonion", DEFAULT_LISTEN_ONION))
        StartTorControl(threadGroup);

    StartNode(threadGroup);

#ifdef ENABLE_WALLET
    // Generate coins in the background
    if (pwalletMain)
        GenerateDivi(settings.GetBoolArg("-gen", false), pwalletMain, settings.GetArg("-genproclimit", 1));
#endif

    // ********************************************************* Step 12: finished

    SetRPCWarmupFinished();
    uiInterface.InitMessage(translate("Done loading"));

#ifdef ENABLE_WALLET
    if (pwalletMain) {
        // Add wallet transactions that aren't already in a block to mapTransactions
        pwalletMain->ReacceptWalletTransactions();

        // Run a thread to flush wallet periodically
        threadGroup.create_thread(boost::bind(&ThreadFlushWalletDB, boost::ref(pwalletMain->strWalletFile)));
    }
#endif

    return !fRequestShutdown;
}
