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
#include <DataDirectory.h>
#include <defaultValues.h>
#include <dbenv.h>
#include "key.h"
#include "main.h"
#include "obfuscation.h"
#include <WalletBackupFeatureContainer.h>
#include <miner.h>
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
#include <timeIntervalConstants.h>
#include <TransactionInputChecker.h>
#include <txmempool.h>
#include <StartAndShutdownSignals.h>
#include <MerkleTxConfirmationNumberCalculator.h>
#include <I_BlockSubmitter.h>
#include <ThreadManagementHelpers.h>

#ifdef ENABLE_WALLET
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

extern CCriticalSection cs_main;
extern Settings& settings;
extern bool fReindex;
extern bool fImporting;
extern bool fVerifyingBlocks;
#if ENABLE_ZMQ
static CZMQNotificationInterface* pzmqNotificationInterface = NULL;
#endif
#ifdef ENABLE_WALLET
std::unique_ptr<I_MerkleTxConfirmationNumberCalculator> confirmationsCalculator(nullptr);
CWallet* pwalletMain = NULL;
constexpr int nWalletBackups = 20;

/**
 * Wallet Settings
 */
#endif

CClientUIInterface uiInterface;

const FeeAndPriorityCalculator& feeAndPriorityCalculator = FeeAndPriorityCalculator::instance();
static CTxMemPool mempool;
CTxMemPool& GetTransactionMemoryPool()
{
    return mempool;
}

/** Global instance of the SporkManager, managed through startup/shutdown.  */
std::unique_ptr<CSporkManager> sporkManagerInstance;

void ThreadFlushWalletDB(Settings& settings, const std::string& strFile)
{
    // Make this thread recognisable as the wallet flushing thread
    RenameThread("divi-wallet");

    static bool fOneThread;
    if (fOneThread)
        return;
    fOneThread = true;

    CWalletDB walletDbToFlush(settings,strFile,"flush");
    const unsigned& walletDbUpdated = walletDbToFlush.numberOfWalletUpdates();
    unsigned int nLastSeen =  walletDbUpdated;
    unsigned int nLastFlushed = walletDbUpdated;
    int64_t nLastWalletUpdate = GetTime();
    while (true) {
        MilliSleep(500);

        if (nLastSeen != walletDbUpdated) {
            nLastSeen = walletDbUpdated;
            nLastWalletUpdate = GetTime();
        }

        if (nLastFlushed != walletDbUpdated && GetTime() - nLastWalletUpdate >= 2) {
            if (walletDbToFlush.Flush())
            {
                nLastFlushed = walletDbUpdated;
            }
        }
    }
}

bool ManualBackupWallet(Settings& settings, const std::string& walletDBFilename, const std::string& strDest)
{
    CWalletDB walletDb(settings,walletDBFilename,"flush");
    while (true) {
        {
            const CWalletDB::BackupStatus status = walletDb.Backup(strDest);
            if(status == CWalletDB::BackupStatus::FAILED_FILESYSTEM_ERROR)
            {
                return false;
            }
            else if (status == CWalletDB::BackupStatus::SUCCEEDED)
            {
                return true;
            }
        }
        MilliSleep(100);
    }
    return false;
}

void InitializeWallet(std::string strWalletFile)
{
#ifdef ENABLE_WALLET
    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();
    const auto& blockMap = chainstate->GetBlockMap();
    confirmationsCalculator.reset(
        new MerkleTxConfirmationNumberCalculator(
            chain,
            blockMap,
            Params().COINBASE_MATURITY(),
            GetTransactionMemoryPool(),
            cs_main));
    pwalletMain = new CWallet(strWalletFile, chain, blockMap, *confirmationsCalculator);
#endif
}

class BlockSubmitter final: public I_BlockSubmitter
{
private:
    bool IsBlockValidChainExtension(CBlock* pblock) const
    {
        {
            LOCK(cs_main);
            const ChainstateManager::Reference chainstate;
            if (pblock->hashPrevBlock != chainstate->ActiveChain().Tip()->GetBlockHash())
                return error("%s : generated block is stale",__func__);
        }
        return true;
    }
public:
    bool submitBlockForChainExtension(CBlock& block) const override
    {
        LogPrintf("%s\n", block);
        LogPrintf("generated %s\n", FormatMoney(block.vtx[0].vout[0].nValue));

        ChainstateManager::Reference chainstate;

        // Process this block the same as if we had received it from another node
        CValidationState state;
        if (!IsBlockValidChainExtension(&block) || !ProcessNewBlock(*chainstate, GetSporkManager(), state, NULL, &block))
            return error("%s : block not accepted",__func__);

        return true;
    }
    static const BlockSubmitter& instance()
    {
        static BlockSubmitter blockSubmitter;
        return blockSubmitter;
    }
};

void StartCoinMintingModule(boost::thread_group& threadGroup, I_StakingWallet* pwalletMain)
{
    // ppcoin:mint proof-of-stake blocks in the background - except on regtest where we want granular control
    InitializeCoinMintingModule(
        settings,
        Params(),
        GetMasternodeModule(),
        GetSporkManager(),
        feeAndPriorityCalculator.getMinimumRelayFeeRate(),
        GetPeerBlockNotifyService(),
        BlockSubmitter::instance(),
        cs_main,
        GetTransactionMemoryPool(),
        pwalletMain);
    const bool underRegressionTesting = Params().NetworkID() == CBaseChainParams::REGTEST;
    if (!underRegressionTesting && pwalletMain && settings.GetBoolArg("-staking", true))
    {
        threadGroup.create_thread(
            boost::bind(
                &TraceThread<void (*)()>,
                "coinmint",
                &ThreadCoinMinter));
    }
}

namespace
{

bool InitError(const std::string& str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_ERROR);
    return false;
}

bool InitWarning(const std::string& str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_WARNING);
    return true;
}

} // anonymous namespace

void DeallocateWallet()
{
#ifdef ENABLE_WALLET
    delete pwalletMain;
    pwalletMain = nullptr;
    confirmationsCalculator.reset();
#endif
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

namespace
{

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


#ifdef ENABLE_WALLET
inline void FlushWallet(bool shutdown = false) { if(pwalletMain) BerkleyDBEnvWrapper().Flush(shutdown);}
#endif

/** Global instance of the ChainstateManager.  The lifetime is managed through
 *  the startup/shutdown cycle.  */
std::unique_ptr<ChainstateManager> chainstateInstance;

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
    StopRPCThreads();
    StopNode();
    DestructCoinMintingModule();
    InterruptTorControl();
    StopTorControl();
    SaveMasternodeDataToDisk();
    UnregisterNodeSignals(GetNodeSignals());

    {
        LOCK(cs_main);
        FlushStateToDisk();
        //record that client took the proper shutdown procedure
        chainstateInstance->BlockTree().WriteFlag("shutdown", true);
        sporkManagerInstance.reset();
        chainstateInstance.reset ();
    }

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
    DeallocateWallet();
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

StartAndShutdownSignals& startAndShutdownSignals = StartAndShutdownSignals::instance();

} // anonymous namespace

bool VerifyChain(int nCheckLevel, int nCheckDepth, bool useCoinTip)
{
    AssertLockHeld(cs_main);
    ChainstateManager::Reference chainstate;
    const CVerifyDB dbVerifier(
        *chainstate,
        GetSporkManager(),
        uiInterface,
        chainstate->GetNominalViewCacheSize(),
        &ShutdownRequested);
    const CCoinsView& coinView =
        useCoinTip
        ? static_cast<const CCoinsView&>(chainstate->CoinsTip())
        : static_cast<const CCoinsView&>(chainstate->GetNonCatchingCoinsView());
    return dbVerifier.VerifyDB(&coinView, chainstate->CoinsTip().GetCacheSize(), nCheckLevel, nCheckDepth);
}

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
    /* At this point, the ChainstateManager doesn't exist anymore (and thus
       there is also no need to free any of its contents).  */
    UnloadBlockIndex(nullptr);
}

namespace
{

/**
 * Signal handlers are very limited in what they are allowed to do, so:
 */
void HandleSIGTERM(int)
{
    fRequestShutdown = true;
}

void HandleSIGHUP(int)
{
    requestReopeningDebugLog();
}

void BlockNotifyCallback(const uint256& hashNewTip)
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

void ReconstructBlockIndex(ChainstateManager& chainstate, const CSporkManager& sporkManager)
{
    // -reindex
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
        LoadExternalBlockFile(chainstate, sporkManager, file, &pos);
        nFile++;
    }
    chainstate.BlockTree().WriteReindexing(false);
    fReindex = false;
    LogPrintf("Reindexing finished\n");
    // To avoid ending up in a situation without genesis block, re-try initializing (no-op if reindexing worked):
    InitBlockIndex(chainstate, sporkManager);
}

void ReindexAndImportBlockFiles(ChainstateManager* chainstate, const CSporkManager* sporkManager, const Settings& settings)
{
    RenameThread("divi-loadblk");

    if(fReindex) ReconstructBlockIndex(*chainstate, *sporkManager);
    std::vector<boost::filesystem::path> vImportFiles;
    if(settings.ParameterIsSet("-loadblock"))
    {
        BOOST_FOREACH (std::string strFile, settings.GetMultiParameter("-loadblock"))
            vImportFiles.push_back(strFile);
    }

    // hardcoded $DATADIR/bootstrap.dat
    boost::filesystem::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (boost::filesystem::exists(pathBootstrap)) {
        FILE* file = fopen(pathBootstrap.string().c_str(), "rb");
        if (file) {
            CImportingNow imp;
            boost::filesystem::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            LogPrintf("Importing bootstrap.dat...\n");
            LoadExternalBlockFile(*chainstate, *sporkManager, file);
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
            LoadExternalBlockFile(*chainstate, *sporkManager, file);
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
    GetTransactionMemoryPool().setSanityCheck(settings.GetBoolArg("-checkmempool", Params().DefaultConsistencyChecks()));
    CCheckpointServices::fEnabled = settings.GetBoolArg("-checkpoints", true);
}

void SetNumberOfThreadsToCheckScripts()
{
    // -par=0 means autodetect, but scriptCheckingThreadCount==0 means no concurrency
    int scriptCheckingThreadCount = settings.GetArg("-par", DEFAULT_SCRIPTCHECK_THREADS);
    if (scriptCheckingThreadCount <= 0)
        scriptCheckingThreadCount += boost::thread::hardware_concurrency();
    if (scriptCheckingThreadCount <= 1)
        scriptCheckingThreadCount = 0;
    else if (scriptCheckingThreadCount > MAX_SCRIPTCHECK_THREADS)
        scriptCheckingThreadCount = MAX_SCRIPTCHECK_THREADS;
    TransactionInputChecker::SetScriptCheckingThreadCount(scriptCheckingThreadCount);
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
        const CAmount nHighTransactionMaxFeeWarning = 10 * COIN;
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
    const std::string expectedWalletFilename = boost::filesystem::basename(strWalletFile) + boost::filesystem::extension(strWalletFile);
    if (strWalletFile != expectedWalletFilename)
    {
        return InitError(strprintf(translate("Unexpected wallet filename %s"), strWalletFile));
    }

    boost::filesystem::path pathToWallet = boost::filesystem::path(strDataDir) / strWalletFile;
    if(boost::filesystem::exists(pathToWallet))
    {
        if(settings.ParameterIsSet("-hdseed") || settings.ParameterIsSet("-mnemonic"))
        {
            try
            {
                boost::filesystem::path pathDest = boost::filesystem::path(strDataDir) / ( strWalletFile + std::to_string(GetTime()) + ".moved" );
                boost::filesystem::rename(pathToWallet,pathDest);
            }
            catch(...)
            {
                return  InitError(strprintf(translate("Unable to move wallet out of the way for seed restore: %s"), pathToWallet.string()));
            }
        }
    }
    return true;
}

void StartScriptVerificationThreads(boost::thread_group& threadGroup)
{
    TransactionInputChecker::InitializeScriptCheckingThreads(threadGroup);
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

void ThreadBackupWallet(const std::string& walletFileName)
{
    static WalletBackupFeatureContainer walletBackupFeatureContainer(static_cast<int>(settings.GetArg("-monthlybackups", 12)), walletFileName, GetDataDir().string());
    WalletBackupFeatureContainer::BackupStatus status = walletBackupFeatureContainer.createMonthlyBackup();
    while (
        status != WalletBackupFeatureContainer::BackupStatus::BACKUP_ATTEMPTED &&
        status != WalletBackupFeatureContainer::BackupStatus::BACKUP_CREATED &&
        status != WalletBackupFeatureContainer::BackupStatus::INTEGRITY_CHECK_FAILED &&
        status != WalletBackupFeatureContainer::BackupStatus::NO_FILE_TO_BACKUP)
    {
        MilliSleep(100);
        status = walletBackupFeatureContainer.createMonthlyBackup();
    }
}

#endif
void BackupWallet(const std::string strDataDir)
{
#ifdef ENABLE_WALLET
    const std::string strWalletFile = settings.GetArg("-wallet", "wallet.dat");
    WalletBackupFeatureContainer walletBackupFeatureContainer(
        settings.GetArg("-createwalletbackups",nWalletBackups), strWalletFile, strDataDir);
    LogPrintf("backing up wallet\n");
    walletBackupFeatureContainer.createCurrentBackup();
    walletBackupFeatureContainer.createMonthlyBackup();
#endif // ENABLE_WALLET
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
    if (!ShouldLogTimestamps()) LogPrintf("Startup time: %s\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()));
    LogPrintf("Default data directory %s\n", GetDefaultDataDir().string());
    LogPrintf("Using data directory %s\n", dataDirectoryInUse);
    LogPrintf("Using config file %s\n", settings.GetConfigFile().string());
    LogPrintf("Using at most %i connections (%i file descriptors available)\n", maximumNumberOfConnections, numberOfFileDescriptors);
    LogPrintf("Using %u threads for script verification\n", TransactionInputChecker::GetScriptCheckingThreadCount());
}

bool SetSporkKey(CSporkManager& sporkManager)
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

struct CoinCacheSizes
{
    size_t nTotalCache;
    size_t nBlockTreeDBCache;
    size_t nCoinDBCache;
    unsigned int nCoinCacheSize;
    CoinCacheSizes(
        ): nTotalCache(settings.GetArg("-dbcache", DEFAULT_DB_CACHE_SIZE) << 20)
        , nBlockTreeDBCache(0)
        , nCoinDBCache(0)
        , nCoinCacheSize(5000)
    {
    }
};

CoinCacheSizes CalculateDBCacheSizes()
{
    CoinCacheSizes cacheSizes;
    size_t& nTotalCache = cacheSizes.nTotalCache;
    size_t& nBlockTreeDBCache = cacheSizes.nBlockTreeDBCache;
    size_t& nCoinDBCache = cacheSizes.nCoinDBCache;
    unsigned int& nCoinCacheSize = cacheSizes.nCoinCacheSize;

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

    return cacheSizes;
}

enum class BlockLoadingStatus {RETRY_LOADING,FAILED_LOADING,SUCCESS_LOADING};

BlockLoadingStatus TryToLoadBlocks(CSporkManager& sporkManager, std::string& strLoadError)
{
    if(fReindex) uiInterface.InitMessage(translate("Reindexing requested. Skip loading block index..."));
    try {
        ChainstateManager::Reference chainstate;
        auto& blockMap = chainstate->GetBlockMap();

        UnloadBlockIndex(&*chainstate);

        if (fReindex)
            chainstate->BlockTree().WriteReindexing(true);

        // DIVI: load previous sessions sporks if we have them.
        uiInterface.InitMessage(translate("Loading sporks..."));
        sporkManager.LoadSporksFromDB();

        if(!fReindex) uiInterface.InitMessage(translate("Loading block index..."));
        std::string strBlockIndexError = "";
        if (!LoadBlockIndex(strBlockIndexError)) {
            strLoadError = translate("Error loading block database");
            strLoadError = strprintf("%s : %s", strLoadError, strBlockIndexError);
            return BlockLoadingStatus::RETRY_LOADING;
        }

        // If the loaded chain has a wrong genesis, bail out immediately
        // (we're likely using a testnet datadir, or the other way around).
        uiInterface.InitMessage(translate("Checking genesis block..."));
        if (!blockMap.empty() && blockMap.count(Params().HashGenesisBlock()) == 0)
        {
            InitError(translate("Incorrect or no genesis block found. Wrong datadir for network?"));
            return BlockLoadingStatus::FAILED_LOADING;
        }

        // Initialize the block index (no-op if non-empty database was already loaded)
        if(!fReindex) uiInterface.InitMessage(translate("Initializing block index databases..."));
        if (!InitBlockIndex(*chainstate, sporkManager)) {
            strLoadError = translate("Error initializing block database");
            return BlockLoadingStatus::RETRY_LOADING;
        }

        // Check for changed -txindex state
        if (chainstate->BlockTree().GetTxIndexing() != settings.GetBoolArg("-txindex", true)) {
            strLoadError = translate("You need to rebuild the database using -reindex to change -txindex");
            return BlockLoadingStatus::RETRY_LOADING;
        }

        uiInterface.InitMessage(translate("Verifying blocks..."));

        // Flag sent to validation code to let it know it can skip certain checks
        fVerifyingBlocks = true;

        {

            LOCK(cs_main);
            if (!VerifyChain(4, settings.GetArg("-checkblocks", 100),false))
            {
                strLoadError = translate("Corrupted block database detected");
                fVerifyingBlocks = false;
                return BlockLoadingStatus::RETRY_LOADING;
            }
        }
    } catch (std::exception& e) {
        if (settings.debugModeIsEnabled()) LogPrintf("%s\n", e.what());
        strLoadError = translate("Error opening block database");
        fVerifyingBlocks = false;
        return BlockLoadingStatus::RETRY_LOADING;
    }

    fVerifyingBlocks = false;
    return BlockLoadingStatus::SUCCESS_LOADING;
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

enum LoadWalletResult
{
    NEW_WALLET_CREATED,
    EXISTING_WALLET_LOADED,
    ERROR_LOADING_WALLET,
};

LoadWalletResult LoadWallet(const std::string strWalletFile, std::ostringstream& strErrors)
{
    InitializeWallet(strWalletFile);
    pwalletMain->NotifyTransactionChanged.connect(&ExternalNotificationScript);
    DBErrors nLoadWalletRet = pwalletMain->LoadWallet();
    const bool fFirstRun = nLoadWalletRet == DB_LOAD_OK_FIRST_RUN;
    if(nLoadWalletRet != DB_LOAD_OK && (nLoadWalletRet==DB_LOAD_OK_FIRST_RUN || nLoadWalletRet == DB_LOAD_OK_RELOAD))
        nLoadWalletRet = DB_LOAD_OK;

    if (nLoadWalletRet != DB_LOAD_OK)
    {
        if (nLoadWalletRet == DB_CORRUPT)
        {
            strErrors << translate("Error loading wallet.dat: Wallet corrupted") << "\n";
            return ERROR_LOADING_WALLET;
        }
        else if (nLoadWalletRet == DB_NONCRITICAL_ERROR)
        {
            std::string msg(translate("Warning: error reading wallet.dat! All keys read correctly, but transaction data"
                            " or address book entries might be missing or incorrect."));
            InitWarning(msg);
        }
        else if (nLoadWalletRet == DB_TOO_NEW)
        {
            strErrors << translate("Loading newer wallet.dat: wallet may require newer version of DIVI Core to run properly") << "\n";
            InitWarning(strErrors.str());
        }
        else if (nLoadWalletRet == DB_NEED_REWRITE || nLoadWalletRet == DB_REWRITTEN)
        {
            strErrors << translate("Wallet needed to be rewritten: restart DIVI Core to complete") << "\n";
            LogPrintf("%s", strErrors.str());
            return ERROR_LOADING_WALLET;
        }
        else
        {
            strErrors << translate("Error loading wallet.dat: database load failure") << "\n";
            return ERROR_LOADING_WALLET;
        }
    }
    return fFirstRun? NEW_WALLET_CREATED : EXISTING_WALLET_LOADED;
}

bool InitializeWalletHDAndChainState(std::ostringstream& strErrors)
{
     // Create new keyUser and set as default key
    if (settings.GetBoolArg("-usehd", DEFAULT_USE_HD_WALLET) && !pwalletMain->IsHDEnabled())
    {
        if (settings.GetArg("-mnemonicpassphrase", "").size() > 256)
        {
            strErrors << translate("Mnemonic passphrase is too long, must be at most 256 characters") << "\n";
            return false;
        }

        try
        {
            pwalletMain->GenerateNewHDChain(); // generate a new master key
        }
        catch(...)
        {
            strErrors << translate("Cannot initialize hd chain or cannot write default address") << "\n";
            return false;
        }
    }
    return true;
}

bool EnsureWalletHDIsNotChanged(std::ostringstream& strErrors)
{
    bool useHD = settings.GetBoolArg("-usehd", DEFAULT_USE_HD_WALLET);
    if (pwalletMain->IsHDEnabled() && !useHD)
    {
        strErrors << strprintf(translate("Error loading %s: You can't disable HD on a already existing HD wallet"), pwalletMain->dbFilename()) << "\n";
        return false;
    }
    if (!pwalletMain->IsHDEnabled() && useHD) {
        strErrors << strprintf(translate("Error loading %s: You can't enable HD on a already existing non-HD wallet"), pwalletMain->dbFilename()) << "\n";
        return false;
    }
    return true;
}

bool CreateNewWalletIfOneIsNotAvailable(std::string strWalletFile, std::ostringstream& strErrors)
{
    const LoadWalletResult loadResult = LoadWallet(strWalletFile, strErrors);
    bool fFirstRun = false;
    switch(loadResult)
    {
        case ERROR_LOADING_WALLET:
            return false;
        case NEW_WALLET_CREATED:
            fFirstRun = true;
            break;
        case EXISTING_WALLET_LOADED:
            fFirstRun = false;
            break;
    }

    if (fFirstRun && !InitializeWalletHDAndChainState(strErrors))
    {
        return false;
    }
    if (!fFirstRun && settings.ParameterIsSet("-usehd") && !EnsureWalletHDIsNotChanged(strErrors))
    {
        return false;
    }

    // Warn user every time he starts non-encrypted HD wallet
    if (!settings.GetBoolArg("-allowunencryptedwallet", false) && settings.GetBoolArg("-usehd", DEFAULT_USE_HD_WALLET) && !pwalletMain->IsLocked())
    {
        InitWarning(translate("Make sure to encrypt your wallet and delete all non-encrypted backups after you verified that wallet works!"));
    }
    return true;
}

void ScanBlockchainForWalletUpdates()
{
    int64_t nStart = GetTimeMillis();
    uiInterface.InitMessage(translate("Scanning chain for wallet updates..."));
    BlockDiskDataReader reader;
    pwalletMain->verifySyncToActiveChain(reader,settings.GetBoolArg("-rescan", false));
    LogPrintf(" rescan      %15dms\n", GetTimeMillis() - nStart);
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

bool LookupMasternodeKey(Settings& settings, CWallet* pwallet, std::string& errorMessage)
{
    settings.ForceRemoveArg("-masternodeprivkey");
    if(settings.ParameterIsSet("-masternode"))
    {
        std::string alias = settings.GetArg("-masternode","");
        if(alias.empty()) return false;
        // Pull up keyID by alias and pass it downstream with keystore for lookup of the private key
        // Potential issues may arise in the event that a person reuses the same alias for a differnt
        // MN - should assert here that if duplicate potential addresses are found that it will pick
        // one and maybe not the one that was originally intended
        alias = "reserved->"+alias;
        for(const std::pair<CTxDestination, AddressLabel>& addressData: pwallet->GetAddressBookManager().GetAddressBook())
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
                return true;
            }
        }
        return false;
    }
    return true;
}

void SubmitUnconfirmedWalletTransactionsToMempool(const CWallet& wallet)
{
    LOCK2(cs_main, wallet.cs_wallet);
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator = wallet.getConfirmationCalculator();
    for(const std::pair<int64_t,const CWalletTx*>& item: wallet.OrderedTxItems())
    {
        const CWalletTx& wtx = *(item.second);
        if (!wtx.IsCoinBase() && !wtx.IsCoinStake() && confsCalculator.GetNumberOfBlockConfirmations(wtx) < 0)
        {
            // Try to add to memory pool
            SubmitTransactionToMempool(GetTransactionMemoryPool(),wtx);
        }
    }
}

} // anonymous namespace

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

    WarmUpRPCAndStartRPCThreads();


    // ********************************************************* Step 5: Backup wallet and verify wallet database integrity
    if(!fDisableWallet) BackupWallet(strDataDir);
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

    uiInterface.InitMessage(translate("Preparing databases..."));
    const auto cacheSizes = CalculateDBCacheSizes();
    chainstateInstance.reset(new ChainstateManager (cacheSizes.nBlockTreeDBCache, cacheSizes.nCoinDBCache,cacheSizes.nCoinCacheSize, false, fReindex));
    const auto& chainActive = chainstateInstance->ActiveChain();
    const auto& blockMap = chainstateInstance->GetBlockMap();
    sporkManagerInstance.reset(new CSporkManager(*chainstateInstance));

    if(!SetSporkKey(*sporkManagerInstance))
        return false;

    bool fLoaded = false;
    int64_t nStart;
    while (!fLoaded) {
        bool fReset = fReindex;
        std::string strLoadError;

        uiInterface.InitMessage(translate("Loading block index..."));

        nStart = GetTimeMillis();
        const BlockLoadingStatus status = TryToLoadBlocks(*sporkManagerInstance, strLoadError);
        fLoaded = (status == BlockLoadingStatus::SUCCESS_LOADING);
        if(!fLoaded && status != BlockLoadingStatus::RETRY_LOADING)
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
            return InitError(strErrors.str());
        }
        if(settings.GetBoolArg("-vault", false))
        {
            // Off by default in wallet
            pwalletMain->activateVaultMode();
        }


        LogPrintf("%s", strErrors.str());
        LogPrintf(" wallet      %15dms\n", GetTimeMillis() - nStart);

        RegisterValidationInterface(pwalletMain);

        ScanBlockchainForWalletUpdates();
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
    if (!ActivateBestChain(*chainstateInstance, *sporkManagerInstance, state))
        strErrors << "Failed to connect best block";
#ifdef ENABLE_WALLET
    if(settings.ParameterIsSet("-prunewalletconfs"))
    {
        pwalletMain->PruneWallet();
    }
#endif

    threadGroup.create_thread(boost::bind(&ReindexAndImportBlockFiles, chainstateInstance.get(), sporkManagerInstance.get(), settings));

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
    if(!LookupMasternodeKey(settings,pwalletMain,errorMessage))
    {
        return InitError("Unknown key or missing label for masternode=<alias>. masternode=<alias> may be missing from configuration.");
    }
    {
    if(!InitializeMasternodeIfRequested(settings, chainstateInstance->BlockTree().GetTxIndexing(), errorMessage))
    {
        return InitError(errorMessage);
    }
    }
    LockUpMasternodeCollateral();
    threadGroup.create_thread(boost::bind(&ThreadMasternodeBackgroundSync));

    // ********************************************************* Step 11: start node

    if (!CheckDiskSpace())
        return CValidationState().Abort("Disk space is low!");

    if (!strErrors.str().empty())
        return InitError(strErrors.str());

    //// debug print
    LogPrintf("mapBlockIndex.size() = %u\n", blockMap.size());
    LogPrintf("chainActive.Height() = %d\n", chainActive.Height());
#ifdef ENABLE_WALLET
    LogPrintf("Key Pool size = %u\n", pwalletMain ? pwalletMain->GetKeyPoolSize() : 0);
    LogPrintf("Address Book size = %u\n", pwalletMain ? pwalletMain->GetAddressBookManager().GetAddressBook().size() : 0);
#endif

    if (settings.GetBoolArg("-listenonion", DEFAULT_LISTEN_ONION))
        StartTorControl(threadGroup);

    uiInterface.InitMessage(translate("Initializing P2P connections..."));
    StartNode(threadGroup);
    StartCoinMintingModule(threadGroup,pwalletMain);

    // ********************************************************* Step 12: finished

    SetRPCWarmupFinished();
    uiInterface.InitMessage(translate("Done loading"));

#ifdef ENABLE_WALLET
    if (pwalletMain) {
        // Add wallet transactions that aren't already in a block to mapTransactions
        SubmitUnconfirmedWalletTransactionsToMempool(*pwalletMain);
        if(settings.ParameterIsSet("-prunewalletconfs"))
        {
            if(!pwalletMain->PruneWallet())
            {
                StartShutdown();
                LogPrintf("Failed to prune wallet correctly!");
                return false;
            }
        }
        // Run a thread to flush wallet periodically
        if (settings.GetBoolArg("-flushwallet", true))
        {
            threadGroup.create_thread(boost::bind(&ThreadFlushWalletDB, settings, pwalletMain->dbFilename() ));
        }
        if(pwalletMain)
        {
            const bool underRegressionTesting = Params().NetworkID() == CBaseChainParams::REGTEST;
            int64_t millisecondDelay = NUMBER_OF_SECONDS_IN_A_DAY * 1000;
            threadGroup.create_thread(
                (!underRegressionTesting)?
                boost::bind(&LoopForever<void (*)(const std::string&), const std::string&>, "monthly_backup", &ThreadBackupWallet, pwalletMain->dbFilename(), millisecondDelay):
                boost::bind(&MockLoopForever<void (*)(const std::string&), const std::string&>, "monthly_backup", &ThreadBackupWallet, pwalletMain->dbFilename(), millisecondDelay)
                );
        }
    }
#endif

    return !fRequestShutdown;
}
