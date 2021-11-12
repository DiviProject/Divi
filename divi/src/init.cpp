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

#include "amount.h"
#include <blockmap.h>
#include <base58.h>
#include "BlockFileOpener.h"
#include <BlockDiskAccessor.h>
#include <chain.h>
#include <chainparams.h>
#include "checkpoints.h"
#include "compat/sanity.h"
#include <defaultValues.h>
#include "key.h"
#include "main.h"
#include "obfuscation.h"
#include <WalletBackupFeatureContainer.h>
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
#include <MasternodeModule.h>
#include <functional>
#include <uiMessenger.h>
#include <ActiveChainManager.h>
#include <BlockDiskAccessor.h>
#include <TransactionInputChecker.h>
#include <txmempool.h>
#include <WalletRescanner.h>
#include <StartAndShutdownSignals.h>

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
constexpr int nWalletBackups = 20;

/**
 * Wallet Settings
 */
#endif
extern CCriticalSection cs_main;
extern CChain chainActive;
extern Settings& settings;
extern bool fReindex;
extern bool fImporting;
extern bool fCheckBlockIndex;
extern int nScriptCheckThreads;
extern int nCoinCacheSize;
extern bool fTxIndex;
extern bool fVerifyingBlocks;
extern bool fLiteMode;
extern BlockMap mapBlockIndex;
extern Settings& settings;
extern CBlockTreeDB* pblocktree;
extern CCoinsViewCache* pcoinsTip;
#if ENABLE_ZMQ
static CZMQNotificationInterface* pzmqNotificationInterface = NULL;
#endif

//! -paytxfee will warn if called with a higher fee than this amount (in satoshis) per KB
static const CAmount nHighTransactionFeeWarning = 0.1 * COIN;
//! -maxtxfee will warn if called with a higher fee than this amount (in satoshis)
static const CAmount nHighTransactionMaxFeeWarning = 100 * nHighTransactionFeeWarning;

constexpr char FEE_ESTIMATES_FILENAME[] = "fee_estimates.dat";
CClientUIInterface uiInterface;

bool fAddressIndex = false;
bool fSpentIndex = false;
const FeeAndPriorityCalculator& feeAndPriorityCalculator = FeeAndPriorityCalculator::instance();
CTxMemPool mempool(feeAndPriorityCalculator.getMinimumRelayFeeRate(), fAddressIndex, fSpentIndex);

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
volatile bool fRestartRequested = false; // if true then restart, else shutdown
void MainStartShutdown()
{
    fRequestShutdown = true;
}
bool MainShutdownRequested()
{
    return fRequestShutdown || fRestartRequested;
}

class CCoinsViewErrorCatcher : public CCoinsViewBacked
{
public:
    CCoinsViewErrorCatcher(CCoinsView* view) : CCoinsViewBacked(view) {}
    bool GetCoins(const uint256& txid, CCoins& coins) const override
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
    SetPoWThreadPool(NULL, 0);
#endif
}

void LoadFeeEstimatesForMempool()
{
    boost::filesystem::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
    CAutoFile est_filein(fopen(est_path.string().c_str(), "rb"), SER_DISK, CLIENT_VERSION);
    // Allowed to fail as this file IS missing on first startup.
    if (!est_filein.IsNull())
        mempool.ReadFeeEstimates(est_filein);
}

void SaveFeeEstimatesFromMempool()
{
    if (settings.GetArg("-savemempoolfees",false))
    {
        boost::filesystem::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
        CAutoFile est_fileout(fopen(est_path.string().c_str(), "wb"), SER_DISK, CLIENT_VERSION);
        if (!est_fileout.IsNull())
            mempool.WriteFeeEstimates(est_fileout);
        else
            LogPrintf("%s: Failed to write fee estimates to %s\n", __func__, est_path.string());
    }
}

void DeallocateShallowDatabases()
{
    delete pcoinsTip;
    delete pcoinscatcher;
    delete pcoinsdbview;
    delete pblocktree;

    pcoinsTip = NULL;
    pcoinscatcher = NULL;
    pcoinsdbview = NULL;
    pblocktree = NULL;
    GetSporkManager().DeallocateDatabase();
}

void CleanAndReallocateShallowDatabases(const std::pair<std::size_t,std::size_t>& blockTreeAndCoinDBCacheSizes)
{
    DeallocateShallowDatabases();
    GetSporkManager().AllocateDatabase();
    pblocktree = new CBlockTreeDB(blockTreeAndCoinDBCacheSizes.first, false, fReindex);
    pcoinsdbview = new CCoinsViewDB(mapBlockIndex,blockTreeAndCoinDBCacheSizes.second, false, fReindex);
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
    SaveMasternodeDataToDisk();
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
    boost::filesystem::remove(GetPidFile(settings));
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
void MainShutdown()
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
    CleanupP2PConnections();
    LogPrintf("%s: done\n", __func__);
}

void UnitTestShutdown()
{
  exit(0);
}

void UnitTestStartShutdown()
{
  exit(0);
}

bool UnitTestShutdownRequested()
{
  return false;
}

static StartAndShutdownSignals& startAndShutdownSignals = StartAndShutdownSignals::instance();

void EnableMainSignals()
{
    assert(startAndShutdownSignals.shutdown.empty());
    assert(startAndShutdownSignals.startShutdown.empty());
    assert(startAndShutdownSignals.shutdownRequested.empty());
    startAndShutdownSignals.shutdown.connect(&MainShutdown);
    startAndShutdownSignals.startShutdown.connect(&MainStartShutdown);
    startAndShutdownSignals.shutdownRequested.connect(&MainShutdownRequested);
}

void EnableUnitTestSignals()
{
    assert(startAndShutdownSignals.shutdown.empty());
    assert(startAndShutdownSignals.startShutdown.empty());
    assert(startAndShutdownSignals.shutdownRequested.empty());
    startAndShutdownSignals.shutdown.connect(&UnitTestShutdown);
    startAndShutdownSignals.startShutdown.connect(&UnitTestStartShutdown);
    startAndShutdownSignals.shutdownRequested.connect(&UnitTestShutdownRequested);
}

void StartShutdown()
{
    assert(!startAndShutdownSignals.startShutdown.empty());
    startAndShutdownSignals.startShutdown();
}
bool ShutdownRequested()
{
    assert(!startAndShutdownSignals.shutdownRequested.empty());
    bool val = *startAndShutdownSignals.shutdownRequested();
    return val;
}
void Shutdown()
{
    assert(!startAndShutdownSignals.shutdown.empty());
    startAndShutdownSignals.shutdown();
    UnloadBlockIndex();
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

void ReconstructBlockIndexIfRequested()
{
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
}

void ThreadImport(std::vector<boost::filesystem::path> vImportFiles)
{
    RenameThread("divi-loadblk");

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

bool EnableWalletFeatures()
{
    if (settings.GetBoolArg("-salvagewallet", false)) {
        // Rewrite just private keys: rescan to find transactions
        if (settings.SoftSetBoolArg("-rescan", true))
            LogPrintf("InitializeDivi : parameter interaction: -salvagewallet=1 -> setting -rescan=1\n");
    }
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
    FeeAndPriorityCalculator& feeAndPriorityCalculator = FeeAndPriorityCalculator::instance();
    if (settings.ParameterIsSet("-minrelaytxfee")) {
        CAmount n = 0;
        if (ParseMoney(settings.GetParameter("-minrelaytxfee"), n) && n > 0)
            feeAndPriorityCalculator.setFeeRate(n);
        else
            return InitError(strprintf(translate("Invalid amount for -minrelaytxfee=<amount>: '%s'"), settings.GetParameter("-minrelaytxfee")));
    }
#ifdef ENABLE_WALLET
    if (settings.ParameterIsSet("-maxtxfee")) {
        CAmount maxTxFee;
        if (!ParseMoney(settings.GetParameter("-maxtxfee"), maxTxFee))
            return InitError(strprintf(translate("Invalid amount for -maxtxfee=<amount>: '%s'"), settings.GetParameter("-maxtxfee")));
        if (maxTxFee > nHighTransactionMaxFeeWarning)
            InitWarning(translate("Warning: -maxtxfee is set very high! Fees this large could be paid on a single transaction."));

        const CFeeRate& currentFeeRate = feeAndPriorityCalculator.getMinimumRelayFeeRate();
        if (CFeeRate(maxTxFee, 1000) < currentFeeRate) {
            return InitError(strprintf(translate("Invalid amount for -maxtxfee=<amount>: '%s' (must be at least the minrelay fee of %s to prevent stuck transactions)"),
                settings.GetParameter("-maxtxfee"), currentFeeRate.ToString()));
        }
        feeAndPriorityCalculator.SetMaxFee(maxTxFee);
    }
#endif
    return true;
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
            threadGroup.create_thread(&TransactionInputChecker::ThreadScriptCheck);
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
            LogPrintf("-resync: folder deleted: %s\n", blocksDir.string());
        }

        if (boost::filesystem::exists(chainstateDir)){
            boost::filesystem::remove_all(chainstateDir);
            LogPrintf("-resync: folder deleted: %s\n", chainstateDir.string());
        }

        if (boost::filesystem::exists(sporksDir)){
            boost::filesystem::remove_all(sporksDir);
            LogPrintf("-resync: folder deleted: %s\n", sporksDir.string());
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
    const int maximumNumberOfConnections = GetMaxConnections();
    LogPrintf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    LogPrintf("DIVI version %s (%s)\n", FormatFullVersion(), CLIENT_DATE);
    LogPrintf("Using OpenSSL version %s\n", SSLeay_version(SSLEAY_VERSION));
    if(!fDisableWallet) LogPrintf("Using BerkeleyDB version %s\n", DbEnv::version(0, 0, 0));
    if (!fLogTimestamps) LogPrintf("Startup time: %s\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()));
    LogPrintf("Default data directory %s\n", GetDefaultDataDir().string());
    LogPrintf("Using data directory %s\n", dataDirectoryInUse);
    LogPrintf("Using config file %s\n", settings.GetConfigFile().string());
    LogPrintf("Using at most %i connections (%i file descriptors available)\n", maximumNumberOfConnections, numberOfFileDescriptors);
    LogPrintf("Using %u threads for script verification\n", nScriptCheckThreads);
}

bool SetSporkKey()
{
    CSporkManager& sporkManager = GetSporkManager();
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
    size_t nTotalCache = (settings.GetArg("-dbcache", DEFAULT_DB_CACHE_SIZE) << 20);
    size_t nBlockTreeDBCache = 0;
    size_t nCoinDBCache = 0;
    if (nTotalCache < (MIN_DB_CACHE_SIZE << 20))
        nTotalCache = (MIN_DB_CACHE_SIZE << 20); // total cache cannot be less than MIN_DB_CACHE_SIZE
    else if (nTotalCache > (MAX_DB_CACHE_SIZE << 20))
        nTotalCache = (MAX_DB_CACHE_SIZE << 20); // total cache cannot be greater than MAX_DB_CACHE_SIZE
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
    if(fReindex) uiInterface.InitMessage(translate("Reindexing requested. Skip loading block index..."));
    try {
        uiInterface.InitMessage(translate("Preparing databases..."));
        UnloadBlockIndex();
        std::pair<std::size_t, std::size_t> dbCacheSizes = CalculateDBCacheSizes();
        CleanAndReallocateShallowDatabases(dbCacheSizes);

        if (fReindex)
            pblocktree->WriteReindexing(true);

        // DIVI: load previous sessions sporks if we have them.
        uiInterface.InitMessage(translate("Loading sporks..."));
        GetSporkManager().LoadSporksFromDB();

        if(!fReindex) uiInterface.InitMessage(translate("Loading block index..."));
        std::string strBlockIndexError = "";
        if (!LoadBlockIndex(strBlockIndexError)) {
            strLoadError = translate("Error loading block database");
            strLoadError = strprintf("%s : %s", strLoadError, strBlockIndexError);
            return skipLoadingDueToError;
        }

        // If the loaded chain has a wrong genesis, bail out immediately
        // (we're likely using a testnet datadir, or the other way around).
        uiInterface.InitMessage(translate("Checking genesis block..."));
        if (!mapBlockIndex.empty() && mapBlockIndex.count(Params().HashGenesisBlock()) == 0)
            return InitError(translate("Incorrect or no genesis block found. Wrong datadir for network?"));

        // Initialize the block index (no-op if non-empty database was already loaded)
        if(!fReindex) uiInterface.InitMessage(translate("Initializing block index databases..."));
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

        {

            LOCK(cs_main);
            const ActiveChainManager& chainManager = GetActiveChainManager();
            CVerifyDB dbVerifier(
                chainManager,
                chainActive,
                uiInterface,
                nCoinCacheSize,
                &ShutdownRequested);
            if (!dbVerifier.VerifyDB(pcoinsdbview,pcoinsTip->GetCacheSize(), 4, settings.GetArg("-checkblocks", 100)))
            {
                strLoadError = translate("Corrupted block database detected");
                fVerifyingBlocks = false;
                return skipLoadingDueToError;
            }
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

void ExternalNotificationScript(const uint256& transactionHash,int status)
{
    // notify an external script when a wallet transaction comes in or is updated
    std::string strCmd = settings.GetArg("-walletnotify", "");
    constexpr int hasBeenAddedToWallet = TransactionNotificationType::NEW | TransactionNotificationType::UPDATED;

    if (!strCmd.empty() && (status & hasBeenAddedToWallet)>0)
    {
        boost::replace_all(strCmd, "%s", transactionHash.GetHex());
        boost::thread t(runCommand, strCmd); // thread runs free
    }
}

bool CreateNewWalletIfOneIsNotAvailable(std::string strWalletFile, std::ostringstream& strErrors)
{
    bool fFirstRun = true;
    pwalletMain = new CWallet(strWalletFile, chainActive, mapBlockIndex);
    pwalletMain->NotifyTransactionChanged.connect(&ExternalNotificationScript);
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

        if(!pwalletMain->InitializeDefaultKey())
        {
            return InitError(translate("Cannot write default address") += "\n");
        }

        pwalletMain->UpdateBestBlockLocation();

    }
    else if (settings.ParameterIsSet("-usehd")) {
        bool useHD = settings.GetBoolArg("-usehd", DEFAULT_USE_HD_WALLET);
        if (pwalletMain->IsHDEnabled() && !useHD) {
            InitError(strprintf(translate("Error loading %s: You can't disable HD on a already existing HD wallet"),
                                pwalletMain->dbFilename()));
            return false;
        }
        if (!pwalletMain->IsHDEnabled() && useHD) {
            InitError(strprintf(translate("Error loading %s: You can't enable HD on a already existing non-HD wallet"),
                                pwalletMain->dbFilename()));
            return false;
        }
    }

    // Warn user every time he starts non-encrypted HD wallet
    if (!settings.GetBoolArg("-allowunencryptedwallet", false) && settings.GetBoolArg("-usehd", DEFAULT_USE_HD_WALLET) && !pwalletMain->IsLocked()) {
        InitWarning(translate("Make sure to encrypt your wallet and delete all non-encrypted backups after you verified that wallet works!"));
    }

    return true;
}

int ScanForWalletTransactions(CWallet& walletToRescan, CBlockIndex* scanStartIndex, bool updateWallet)
{
    static BlockDiskDataReader blockReader;
    static WalletRescanner rescanner(blockReader,chainActive,cs_main);
    return rescanner.scanForWalletTransactions(walletToRescan,scanStartIndex,updateWallet);
}

void ScanBlockchainForWalletUpdates(std::string strWalletFile, int64_t& nStart)
{
    BlockDiskDataReader blockReader;
    CBlockIndex* pindexRescan = chainActive.Tip();
    if (settings.GetBoolArg("-rescan", false))
        pindexRescan = chainActive.Genesis();
    else {
        CWalletDB walletdb(settings,strWalletFile);
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
        ScanForWalletTransactions(*pwalletMain,pindexRescan,true);
        LogPrintf(" rescan      %15dms\n", GetTimeMillis() - nStart);
        pwalletMain->UpdateBestBlockLocation();
        pwalletMain->IncrementDBUpdateCount();
    }
}

void LockUpMasternodeCollateral()
{
    if (pwalletMain) {
        LogPrintf("Locking Masternodes:\n");
        LOCK(pwalletMain->cs_wallet);

        CWallet& walletReference = *pwalletMain;
        LockUpMasternodeCollateral(
            settings,
            [&walletReference](const COutPoint& outpoint)
            {
                walletReference.LockCoin(outpoint);
            });
    }
}

void LookupMasternodeKey(Settings& settings, CWallet* pwallet, std::string& errorMessage)
{
    settings.ForceRemoveArg("-masternodeprivkey");
    if(settings.ParameterIsSet("-masternode"))
    {
        std::string alias = settings.GetArg("-masternode","");
        if(alias.empty()) return;
        // Pull up keyID by alias and pass it downstream with keystore for lookup of the private key
        // Potential issues may arise in the event that a person reuses the same alias for a differnt
        // MN - should assert here that if duplicate potential addresses are found that it will pick
        // one and maybe not the one that was originally intended
        alias = "reserved->"+alias;
        for(const std::pair<CTxDestination, CAddressBookData>& addressData: pwallet->GetAddressBook())
        {
            if(addressData.second.name == alias)
            {
                CKeyID keyID = boost::get<CKeyID>(addressData.first);
                CKey mnkey;
                if(!pwallet->GetKey(keyID,mnkey))
                {
                    LogPrintf("%s - Unable to find masternode key\n",__func__);
                }
                settings.SetParameter("-masternodeprivkey",CBitcoinSecret(mnkey).ToString());
            }
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
    UIMessenger uiMessenger(uiInterface);
    SetLoggingAndDebugSettings();

    SetNetworkingParameters();

    if(!EnableWalletFeatures())
    {
        return false;
    }

    // Make sure enough file descriptors are available
    int numberOfFileDescriptors;
    if(!SetNumberOfFileDescriptors(uiMessenger,numberOfFileDescriptors))
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
        if (settings.SoftSetBoolArg("-staking", false))
            LogPrintf("InitializeDivi : parameter interaction: wallet functionality not enabled -> setting -staking=0\n");
    }
    if(!SetTransactionRequirements())
    {
        return false;
    }

    if (settings.ParameterIsSet("-protocolversion")) {
        SetProtocolVersion(settings.GetArg("-protocolversion", PROTOCOL_VERSION));
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
    CreatePidFile(GetPidFile(settings), getpid());
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

    if(!InitializeP2PNetwork(uiMessenger))
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

    LoadFeeEstimatesForMempool();

// ********************************************************* Step 8: load wallet
    std::ostringstream strErrors;
#ifdef ENABLE_WALLET
    const std::string strWalletFile = settings.GetArg("-wallet", "wallet.dat");
    if (fDisableWallet) {
        pwalletMain = NULL;
        LogPrintf("Wallet disabled!\n");
    } else {
        uiInterface.InitMessage(translate("Loading wallet..."));
        fVerifyingBlocks = true;

        nStart = GetTimeMillis();
        if(!CreateNewWalletIfOneIsNotAvailable(strWalletFile,strErrors))
        {
            return false;
        }
        if(settings.GetBoolArg("-spendzeroconfchange", false))
        {
            // Off by default in wallet
            pwalletMain->toggleSpendingZeroConfirmationOutputs();
        }
        if(settings.GetBoolArg("-vault", false))
        {
            // Off by default in wallet
            pwalletMain->activateVaultMode();
        }


        LogPrintf("%s", strErrors.str());
        LogPrintf(" wallet      %15dms\n", GetTimeMillis() - nStart);

        RegisterValidationInterface(pwalletMain);

        ScanBlockchainForWalletUpdates(strWalletFile, nStart);
        fVerifyingBlocks = false;

    }  // (!fDisableWallet)
#else  // ENABLE_WALLET
    LogPrintf("No wallet compiled in!\n");
#endif // !ENABLE_WALLET
    // ********************************************************* Step 9: import blocks

    if (settings.ParameterIsSet("-blocknotify"))
        uiInterface.NotifyBlockTip.connect(BlockNotifyCallback);

    // scan for better chains in the block chain database, that are not yet connected in the active best chain
    uiInterface.InitMessage(translate("Connecting best block..."));
    CValidationState state;
    if (!ActivateBestChain(state))
        strErrors << "Failed to connect best block";

    ReconstructBlockIndexIfRequested();
    if (settings.ParameterIsSet("-loadblock")) {
        std::vector<boost::filesystem::path> vImportFiles;
        BOOST_FOREACH (std::string strFile, settings.GetMultiParameter("-loadblock"))
            vImportFiles.push_back(strFile);
        threadGroup.create_thread(boost::bind(&ThreadImport, vImportFiles));
    }

    if (chainActive.Tip() == NULL) {
        LogPrintf("Waiting for genesis block to be imported...\n");
        while (!fRequestShutdown && chainActive.Tip() == NULL)
            MilliSleep(10);
    }

    // ********************************************************* Step 10: setup ObfuScation
    std::string errorMessage;
    if(!LoadMasternodeDataFromDisk(uiMessenger,GetDataDir().string()) )
    {
        return false;
    }
    uiInterface.InitMessage(translate("Checking for active masternode..."));
    LookupMasternodeKey(settings,pwalletMain,errorMessage);
    if(!InitializeMasternodeIfRequested(settings,fTxIndex,errorMessage))
    {
        return InitError(errorMessage);
    }
    LockUpMasternodeCollateral();
    threadGroup.create_thread(boost::bind(&ThreadMasternodeBackgroundSync));
    LogPrintf("fLiteMode %d\n", fLiteMode);

    // ********************************************************* Step 11: start node

    if (!CheckDiskSpace())
        return CValidationState().Abort("Disk space is low!");

    if (!strErrors.str().empty())
        return InitError(strErrors.str());

    //// debug print
    LogPrintf("mapBlockIndex.size() = %u\n", mapBlockIndex.size());
    LogPrintf("chainActive.Height() = %d\n", chainActive.Height());
#ifdef ENABLE_WALLET
    LogPrintf("Key Pool size = %u\n", pwalletMain ? pwalletMain->GetKeyPoolSize() : 0);
    LogPrintf("Address Book size = %u\n", pwalletMain ? pwalletMain->GetAddressBook().size() : 0);
#endif

    if (settings.GetBoolArg("-listenonion", DEFAULT_LISTEN_ONION))
        StartTorControl(threadGroup);

    uiInterface.InitMessage(translate("Initializing P2P connections..."));
    StartNode(threadGroup,pwalletMain);

#ifdef ENABLE_WALLET
    // Generate coins in the background
    if (pwalletMain && settings.GetBoolArg("-mining", false))
        SetPoWThreadPool(pwalletMain, settings.GetArg("-mining_threads", 1));
#endif

    // ********************************************************* Step 12: finished

    SetRPCWarmupFinished();
    uiInterface.InitMessage(translate("Done loading"));

#ifdef ENABLE_WALLET
    if (pwalletMain) {
        // Add wallet transactions that aren't already in a block to mapTransactions
        pwalletMain->ReacceptWalletTransactions();

        // Run a thread to flush wallet periodically
        if (settings.GetBoolArg("-flushwallet", true))
        {
            threadGroup.create_thread(boost::bind(&ThreadFlushWalletDB, pwalletMain->dbFilename() ));
        }
    }
#endif

    return !fRequestShutdown;
}
