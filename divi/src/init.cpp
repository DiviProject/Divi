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
#include <BlockDiskDataReader.h>
#include <BlockIndexLoading.h>
#include <chain.h>
#include <chainparams.h>
#include <ChainstateManager.h>
#include <clientversion.h>
#include "compat/sanity.h"
#include <DataDirectory.h>
#include <defaultValues.h>
#include <WalletBackupFeatureContainer.h>
#include <miner.h>
#include "net.h"
#include "rpcserver.h"
#include "spork.h"
#include "sporkdb.h"
#include "torcontrol.h"
#include <txdb.h>
#include "ui_interface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "NotificationInterface.h"
#include "FeeAndPriorityCalculator.h"
#include <Settings.h>
#include <MasternodeModule.h>
#include <functional>
#include <uiMessenger.h>
#include <timeIntervalConstants.h>
#include <TransactionInputChecker.h>
#include <txmempool.h>
#include <StartAndShutdownSignals.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>
#include <I_BlockSubmitter.h>
#include <ThreadManagementHelpers.h>
#include <LoadWalletResult.h>
#include <MultiWalletModule.h>
#include <TransactionDiskAccessor.h>
#include <MainNotificationRegistration.h>
#include <ChainSyncHelpers.h>
#include <ChainExtensionModule.h>
#include <BlockInvalidationHelpers.h>
#include <FlushChainState.h>

#ifdef ENABLE_WALLET
#include "wallet.h"
#include "walletdb.h"
#include <WalletTx.h>
#include <LegacyWalletDatabaseEndpointFactory.h>
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
#include <I_ChainExtensionService.h>

#if ENABLE_ZMQ
#include "zmq/zmqnotificationinterface.h"
#endif

#include <ValidationState.h>
#include <verifyDb.h>
#include <stdio.h>

extern Settings& settings;
#if ENABLE_ZMQ
static CZMQNotificationInterface* pzmqNotificationInterface = NULL;
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
/** Global instance of the ChainstateManager.  The lifetime is managed through
 *  the startup/shutdown cycle.  */
std::unique_ptr<ChainstateManager> chainstateInstance;

class P2PNotifications final: public NotificationInterface
{
public:
    void UpdatedBlockTip(const CBlockIndex *pindex) override
    {
        ChainstateManager::Reference chainStateRef;
        uiInterface.NotifyBlockTip(pindex->GetBlockHash());
        NotifyPeersOfNewChainTip(chainStateRef->ActiveChain().Height(),pindex->GetBlockHash(), 100);
        GetTransactionMemoryPool().setLastTimeOfChainTipUpdate(GetTime());
    }
};
std::unique_ptr<P2PNotifications> p2pNotifications;

#ifdef ENABLE_WALLET
constexpr int nWalletBackups = 20;
#endif
static boost::thread_group* globalThreadGroupRef = nullptr;
std::unique_ptr<MultiWalletModule> multiWalletModule(nullptr);
CCriticalSection cs_main;

void FlushStateToDisk()
{
    MainNotificationSignals& notificationSignals = GetMainNotificationInterface();
    CValidationState state;
    ChainstateManager::Reference chainstateRef_;
    FlushStateToDisk(*chainstateRef_, state, FlushStateMode::FLUSH_STATE_ALWAYS, notificationSignals,cs_main);
}

void InitializeMultiWalletModule()
{
    if(multiWalletModule) return;
    assert(chainstateInstance);
    multiWalletModule.reset(
        new MultiWalletModule(
            *chainstateInstance,
            settings,
            GetTransactionMemoryPool(),
            cs_main,
            Params().COINBASE_MATURITY()));
}
void FinalizeMultiWalletModule()
{
    if(!multiWalletModule) return;
    multiWalletModule.reset();
}

typedef std::map<unsigned int, unsigned int> LastExtensionTimestampByBlockHeight;
static LastExtensionTimestampByBlockHeight hashedBlocksByHeight;
bool CheckHeightForRecentProofOfStakeGeneration(const int blockHeight)
{
    constexpr int64_t fiveMinutes = 5*60;
    const auto it = hashedBlocksByHeight.find(blockHeight);
    return it != hashedBlocksByHeight.end() && GetTime() - it->second < fiveMinutes;
}

bool HasRecentlyAttemptedToGenerateProofOfStake()
{
    const ChainstateManager::Reference chainstate;
    int currentChainHeight = chainstate->ActiveChain().Tip()->nHeight;
    for(int offset =0 ; offset < 4; offset++ )
    {
        if(currentChainHeight - offset < 0) break;
        if(CheckHeightForRecentProofOfStakeGeneration(currentChainHeight-offset)) return true;
    }
    return false;
}

std::unique_ptr<ChainExtensionModule> chainExtensionModule;
void InitializeChainExtensionModule(const MasternodeModule& masternodeModule)
{
    assert(chainExtensionModule == nullptr);
    chainExtensionModule.reset(
        new ChainExtensionModule(
            ChainstateManager::Get(),
            GetTransactionMemoryPool(),
            masternodeModule,
            GetMainNotificationInterface(),
            cs_main,
            settings,
            Params(),
            GetSporkManager(),
            hashedBlocksByHeight,
            GetBlockIndexSuccessorsByPreviousBlockIndex(),
            GetBlockIndexCandidates()));
}
void FinalizeChainExtensionModule()
{
    chainExtensionModule.reset();
}
const I_ChainExtensionService& GetChainExtensionService()
{
    return chainExtensionModule->getChainExtensionService();
}
const I_BlockSubmitter& GetBlockSubmitter()
{
    return chainExtensionModule->getBlockSubmitter();
}

bool ManualBackupWallet(const std::string& strDest)
{
    assert(multiWalletModule);
    return  multiWalletModule->getWalletDbEnpointFactory().backupWalletFile(strDest);
}

const I_MerkleTxConfirmationNumberCalculator& GetConfirmationsCalculator()
{
    assert(multiWalletModule);
    return multiWalletModule->getConfirmationsCalculator();
}

CWallet* GetWallet()
{
#ifdef ENABLE_WALLET
    return multiWalletModule->getActiveWallet();
#else
    return nullptr;
#endif
}
std::string GetWalletName()
{
#ifdef ENABLE_WALLET
    return multiWalletModule->getActiveWalletName();
#else
    return 'Wallets currently disabled';
#endif
}
static std::string stakingWalletName = "";

void StartCoinMintingModule(boost::thread_group& threadGroup, I_StakingWallet& stakingWallet)
{
    // ppcoin:mint proof-of-stake blocks in the background - except on regtest where we want granular control
    const bool underRegressionTesting = Params().NetworkID() == CBaseChainParams::REGTEST;
    if(underRegressionTesting && !settings.ParameterIsSet("-staking")) settings.SetParameter("-staking", "0");

    InitializeCoinMintingModule(
        settings,
        Params(),
        chainExtensionModule->getBlockProofProver(stakingWallet),
        GetMasternodeModule().getMasternodeSynchronization(),
        feeAndPriorityCalculator.getMinimumRelayFeeRate(),
        GetPeerBlockNotifyService(),
        chainExtensionModule->getBlockSubmitter(),
        chainExtensionModule->getDifficultyAdjuster(),
        hashedBlocksByHeight,
        cs_main,
        GetTransactionMemoryPool(),
        stakingWallet,
        threadGroup);
}

bool SwitchCoinMintingModuleToWallet(const std::string activeWalletName)
{
    AssertLockHeld(cs_main);
    if(activeWalletName == stakingWalletName) return true;
    CWallet* stakingWallet = multiWalletModule->getWalletByName(stakingWalletName);

    ShutdownCoinMintingModule();
    UnregisterMainNotificationInterface(stakingWallet);
    if(stakingWallet->IsCrypted()) stakingWallet->LockFully();
    if(LoadAndSelectWallet(activeWalletName, true))
    {
        stakingWalletName = activeWalletName;
    }
    else
    {
        LoadAndSelectWallet(stakingWalletName, true);
    }
    StartCoinMintingModule(*globalThreadGroupRef,*static_cast<I_StakingWallet*>(GetWallet()));
    return stakingWalletName == activeWalletName;
}

void ReloadActiveWallet()
{
    AssertLockHeld(cs_main);
    const std::string activeWalletName = multiWalletModule->getActiveWalletName();
    CWallet* activeWallet = multiWalletModule->getWalletByName(activeWalletName);

    if(activeWalletName == stakingWalletName) ShutdownCoinMintingModule();
    UnregisterMainNotificationInterface(activeWallet);
    multiWalletModule->reloadActiveWallet();
    LoadAndSelectWallet(activeWalletName, true);
    if(activeWalletName == stakingWalletName) StartCoinMintingModule(*globalThreadGroupRef,*static_cast<I_StakingWallet*>(GetWallet()));
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
    ShutdownCoinMintingModule();
    InterruptTorControl();
    StopTorControl();
    SaveMasternodeDataToDisk();
    FinalizeP2PNetwork();

    {
        LOCK(cs_main);
        FlushStateToDisk();
        UnregisterAllMainNotificationInterfaces();
        p2pNotifications.reset();

        //record that client took the proper shutdown procedure
        FinalizeMainBlockchainModules();
    }

#if ENABLE_ZMQ
    if (pzmqNotificationInterface) {
        delete pzmqNotificationInterface;
        pzmqNotificationInterface = NULL;
    }
#endif
#ifndef WIN32
    boost::filesystem::remove(GetPidFile(settings));
#endif
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
    const CCoinsView& coinView =
        useCoinTip
        ? static_cast<const CCoinsView&>(chainstate->CoinsTip())
        : static_cast<const CCoinsView&>(chainstate->GetNonCatchingCoinsView());
    const CVerifyDB dbVerifier(
        Params(),
        chainExtensionModule->getBlockSubsidies(),
        chainExtensionModule->getBlockIncentivesPopulator(),
        *chainstate,
        coinView,
        GetSporkManager(),
        uiInterface,
        chainstate->GetNominalViewCacheSize(),
        &ShutdownRequested);
    return dbVerifier.VerifyDB(nCheckLevel, nCheckDepth);
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
    assert(!chainstateInstance);
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
private:
    Settings& internalSettings_;
public:
    CImportingNow(Settings& settings): internalSettings_(settings)
    {
        assert(internalSettings_.isImportingFiles() == false);
        internalSettings_.setFileImportingFlag(true);
    }

    ~CImportingNow()
    {
        assert(internalSettings_.isImportingFiles() == true);
        internalSettings_.setFileImportingFlag(false);
    }
};

struct CVerifyingNow {
private:
    Settings& internalSettings_;
public:
    CVerifyingNow(Settings& settings): internalSettings_(settings)
    {
        assert(internalSettings_.isStartupVerifyingBlocks() == false);
        internalSettings_.setStartupBlockVerificationFlag(true);
    }

    ~CVerifyingNow()
    {
        assert(internalSettings_.isStartupVerifyingBlocks() == true);
        internalSettings_.setStartupBlockVerificationFlag(false);
    }
};

bool LoadExternalBlockFile(ChainstateManager& chainstate, FILE* fileIn, CDiskBlockPos* dbp = NULL)
{
    // Map of disk positions for blocks with unknown parent (only used for reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    const auto& blockSubmitter = chainExtensionModule->getBlockSubmitter();
    const auto& blockMap = chainstate.GetBlockMap();

    int nLoaded = 0;
    try {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        CBufferedFile blkdat(fileIn, 2 * MAX_BLOCK_SIZE_CURRENT, MAX_BLOCK_SIZE_CURRENT + 8, SER_DISK, CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof()) {
            boost::this_thread::interruption_point();

            blkdat.SetPos(nRewind);
            nRewind++;         // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try {
                // locate a header
                unsigned char buf[MESSAGE_START_SIZE];
                blkdat.FindByte(Params().MessageStart()[0]);
                nRewind = blkdat.GetPos() + 1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, Params().MessageStart(), MESSAGE_START_SIZE))
                    continue;
                // read size
                blkdat >> nSize;
                if (nSize < 80 || nSize > MAX_BLOCK_SIZE_CURRENT)
                    continue;
            } catch (const std::exception&) {
                // no valid block header found; don't complain
                break;
            }
            try {
                // read block
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                CBlock block;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                // detect out of order blocks, and store them for later
                uint256 hash = block.GetHash();
                if (hash != Params().HashGenesisBlock() && blockMap.count(block.hashPrevBlock) == 0) {
                    LogPrint("reindex", "%s: Out of order block %s, parent %s not known\n", __func__, hash,
                             block.hashPrevBlock);
                    if (dbp)
                        mapBlocksUnknownParent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                    continue;
                }

                // process in case the block isn't known yet
                const auto mit = blockMap.find(hash);
                if (mit == blockMap.end() || (mit->second->nStatus & BLOCK_HAVE_DATA) == 0) {
                    CValidationState state;
                    if (blockSubmitter.acceptBlockForChainExtension(state, block, dbp))
                        nLoaded++;
                    if (state.IsError())
                        break;
                } else if (hash != Params().HashGenesisBlock() && mit->second->nHeight % 1000 == 0) {
                    LogPrintf("Block Import: already had block %s at height %d\n", hash, mit->second->nHeight);
                }

                // Recursively process earlier encountered successors of this block
                std::deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty()) {
                    uint256 head = queue.front();
                    queue.pop_front();
                    std::pair<std::multimap<uint256, CDiskBlockPos>::iterator, std::multimap<uint256, CDiskBlockPos>::iterator> range = mapBlocksUnknownParent.equal_range(head);
                    while (range.first != range.second) {
                        std::multimap<uint256, CDiskBlockPos>::iterator it = range.first;
                        if (ReadBlockFromDisk(block, it->second)) {
                            LogPrintf("%s: Processing out of order child %s of %s\n", __func__, block.GetHash(), head);
                            CValidationState dummy;
                            if (blockSubmitter.acceptBlockForChainExtension(dummy, block, &it->second)) {
                                nLoaded++;
                                queue.push_back(block.GetHash());
                            }
                        }
                        range.first++;
                        mapBlocksUnknownParent.erase(it);
                    }
                }
            } catch (std::exception& e) {
                LogPrintf("%s : Deserialize or I/O error - %s", __func__, e.what());
            }
        }
    } catch (std::runtime_error& e) {
        CValidationState().Abort(std::string("System error: ") + e.what());
    }
    if (nLoaded > 0)
        LogPrintf("Loaded %i blocks from external file in %dms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

void ReconstructBlockIndex(ChainstateManager& chainstate)
{
    // -reindex
    CImportingNow imp(settings);
    int nFile = 0;
    while (true) {
        CDiskBlockPos pos(nFile, 0);
        if (!BlockFileExists(pos, "blk"))
            break; // No block files left to reindex
        FILE* file = OpenBlockFile(pos, true);
        if (!file)
            break; // This error is logged in OpenBlockFile
        LogPrintf("Reindexing block file blk%05u.dat...\n", (unsigned int)nFile);
        LoadExternalBlockFile(chainstate, file, &pos);
        nFile++;
    }
    chainstate.BlockTree().WriteReindexing(false);
    settings.setReindexingFlag(false);
    LogPrintf("Reindexing finished\n");
    // To avoid ending up in a situation without genesis block, re-try initializing (no-op if reindexing worked):
    GetChainExtensionService().connectGenesisBlock();
}

void ReindexAndImportBlockFiles(ChainstateManager* chainstate, Settings& settings)
{
    RenameThread("divi-loadblk");

    if(settings.isReindexingBlocks()) ReconstructBlockIndex(*chainstate);
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
            CImportingNow imp(settings);
            boost::filesystem::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            LogPrintf("Importing bootstrap.dat...\n");
            LoadExternalBlockFile(*chainstate, file);
            RenameOver(pathBootstrap, pathBootstrapOld);
        } else {
            LogPrintf("Warning: Could not open bootstrap file %s\n", pathBootstrap.string());
        }
    }

    // -loadblock=
    BOOST_FOREACH (boost::filesystem::path& path, vImportFiles) {
        FILE* file = fopen(path.string().c_str(), "rb");
        if (file) {
            CImportingNow imp(settings);
            LogPrintf("Importing blocks file %s...\n", path.string());
            LoadExternalBlockFile(*chainstate, file);
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
}

void SetNumberOfThreadsToCheckScripts()
{
    // -par=0 means autodetect, but scriptCheckingThreadCount==0 means no concurrency
    TransactionInputChecker::SetScriptCheckingThreadCount(settings.GetArg("-par", DEFAULT_SCRIPTCHECK_THREADS));
}

bool WalletIsDisabled()
{
#ifdef ENABLE_WALLET
    return settings.GetBoolArg("-disablewallet", false);
#else
    settings.SetParameter("-disablewallet", true);
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
    if(settings.reindexingWasRequested())
        settings.setReindexingFlag(true);
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
        if (linked)
        {
            settings.setReindexingFlag(true);
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
    if(settings.isReindexingBlocks()) uiInterface.InitMessage(translate("Reindexing requested. Skip loading block index..."));
    try {
        ChainstateManager::Reference chainstate;
        auto& blockMap = chainstate->GetBlockMap();

        UnloadBlockIndex(&*chainstate);

        if (settings.isReindexingBlocks())
            chainstate->BlockTree().WriteReindexing(true);

        // DIVI: load previous sessions sporks if we have them.
        uiInterface.InitMessage(translate("Loading sporks..."));
        sporkManager.LoadSporksFromDB();

        if(!settings.isReindexingBlocks()) uiInterface.InitMessage(translate("Loading block index..."));
        std::string strBlockIndexError = "";
        if (!LoadBlockIndex(settings, strBlockIndexError)) {
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
        if(!settings.isReindexingBlocks()) uiInterface.InitMessage(translate("Initializing block index databases..."));
        if (!GetChainExtensionService().connectGenesisBlock()) {
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

        {
            LOCK(cs_main);
            CVerifyingNow chainVerificationInProgress(settings);
            if (!VerifyChain(4, settings.GetArg("-checkblocks", 100),false))
            {
                strLoadError = translate("Corrupted block database detected");
                return BlockLoadingStatus::RETRY_LOADING;
            }
        }
    } catch (std::exception& e) {
        if (settings.debugModeIsEnabled()) LogPrintf("%s\n", e.what());
        strLoadError = translate("Error opening block database");
        return BlockLoadingStatus::RETRY_LOADING;
    }

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

LoadWalletResult ParseDbErrorsFromLoadingWallet(DBErrors dbError, std::ostringstream& strErrors)
{
    bool warningDetected = false;
    const bool fFirstRun = dbError == DB_LOAD_OK_FIRST_RUN;
    if(dbError != DB_LOAD_OK && (dbError==DB_LOAD_OK_FIRST_RUN || dbError == DB_LOAD_OK_RELOAD))
        dbError = DB_LOAD_OK;

    if (dbError != DB_LOAD_OK)
    {
        if (dbError == DB_CORRUPT)
        {
            strErrors << std::string("Error loading wallet.dat: Wallet corrupted");
        }
        else if (dbError == DB_NONCRITICAL_ERROR)
        {
            std::string msg("Warning: error reading wallet.dat! All keys read correctly, but transaction data"
                            " or address book entries might be missing or incorrect.");
            strErrors << msg;
            warningDetected = true;
        }
        else if (dbError == DB_TOO_NEW)
        {
            strErrors << std::string("Loading newer wallet.dat: wallet may require newer version of DIVI Core to run properly");
            warningDetected = true;
        }
        else if (dbError == DB_NEED_REWRITE || dbError == DB_REWRITTEN)
        {
            strErrors << std::string("Wallet needed to be rewritten: restart DIVI Core to complete");
        }
        else
        {
            strErrors << std::string("Error loading wallet.dat: database load failure") << "\n";
        }
        return warningDetected? WARNING_LOADING_WALLET: ERROR_LOADING_WALLET;
    }
    return fFirstRun? NEW_WALLET_CREATED : EXISTING_WALLET_LOADED;
}

void LoadWhitelistedVaults(CWallet* wallet)
{
    std::vector<SerializedScript> whitelistedVaultScripts;
    for(const std::string& whitelistedVaultScript: settings.GetMultiParameter("-whitelisted_vault"))
    {
        auto byteVector = ParseHex(whitelistedVaultScript);
        whitelistedVaultScripts.push_back(byteVector);
    }
    wallet->loadWhitelistedVaults(whitelistedVaultScripts);
}

LoadWalletResult LoadWallet(const std::string strWalletFile, std::ostringstream& strErrors)
{
    try
    {
        multiWalletModule->loadWallet(strWalletFile);
        multiWalletModule->setActiveWallet(strWalletFile);
        GetWallet()->NotifyTransactionChanged.connect(&ExternalNotificationScript);
        const auto parsedResult = ParseDbErrorsFromLoadingWallet(GetWallet()->loadWallet(), strErrors);
        if( parsedResult == NEW_WALLET_CREATED || parsedResult == EXISTING_WALLET_LOADED) LoadWhitelistedVaults(GetWallet());
        return parsedResult;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        std::string errorMessage = std::string("Error loading or creating wallet.dat, ")+ e.what();
        strErrors << errorMessage;
        return ERROR_LOADING_WALLET;
    }
}

bool CreateNewWalletIfOneIsNotAvailable(std::string strWalletFile, std::ostringstream& strErrors, bool& errorMessageIsWarning)
{
    const LoadWalletResult loadResult = LoadWallet(strWalletFile, strErrors);
    switch(loadResult)
    {
        case ERROR_LOADING_WALLET:
            errorMessageIsWarning = false;
            return false;
        case WARNING_LOADING_WALLET:
            errorMessageIsWarning = true;
            return false;
        case NEW_WALLET_CREATED: case EXISTING_WALLET_LOADED:
            break;
    }

    // Warn user every time he starts non-encrypted HD wallet
    if (!settings.GetBoolArg("-allowunencryptedwallet", false) && !GetWallet()->IsLocked())
    {
        InitWarning(translate("Make sure to encrypt your wallet and delete all non-encrypted backups after you verified that wallet works!"));
    }
    RegisterMainNotificationInterface(GetWallet());
    return true;
}

void ScanBlockchainForWalletUpdates()
{
    int64_t nStart = GetTimeMillis();
    uiInterface.InitMessage(translate("Scanning chain for wallet updates..."));
    BlockDiskDataReader reader;
    GetWallet()->verifySyncToActiveChain(reader,settings.GetBoolArg("-rescan", false));
    LogPrintf(" rescan      %15dms\n", GetTimeMillis() - nStart);
}

void LockUpMasternodeCollateral()
{
    if (GetWallet()) {
        LogPrintf("Locking Masternodes:\n");
        LOCK(GetWallet()->getWalletCriticalSection());

        CWallet& walletReference = *GetWallet();
        LockUpMasternodeCollateral(
            settings,
            [&walletReference](const COutPoint& outpoint)
            {
                walletReference.LockCoin(outpoint);
            });
    }
}

void SubmitUnconfirmedWalletTransactionsToMempool(const CWallet& wallet)
{
    LOCK2(cs_main, wallet.getWalletCriticalSection());
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator = GetConfirmationsCalculator();
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

void InitializeWalletBackendSettings(const LegacyWalletDatabaseEndpointFactory& dbEndpointFactory)
{
    if (settings.GetBoolArg("-flushwallet", true))
        dbEndpointFactory.enableBackgroundDatabaseFlushing(*globalThreadGroupRef);

    if(settings.GetArg("-monthlybackups",12) > 0)
        dbEndpointFactory.enableBackgroundMonthlyWalletBackup(
            *globalThreadGroupRef,
            GetDataDir().string(),
            Params().NetworkID() == CBaseChainParams::REGTEST);
}

bool LoadAndSelectWallet(const std::string& walletFilename, bool initializeBackendSettings)
{
    assert(multiWalletModule);
    if(WalletIsDisabled()) return false;

    int64_t nStart = GetTimeMillis();
    std::ostringstream errors;
    bool errorMessageIsWarning = false;
    if(!CreateNewWalletIfOneIsNotAvailable(walletFilename,errors,errorMessageIsWarning))
    {
        const std::string sourceErrorMessage = errors.str();
        std::string translatedErrorMessage = translate(sourceErrorMessage.c_str());
        LogPrintf("%s\n%s\n", translate("Errors detected during wallet loading"), translatedErrorMessage );
        if(!errorMessageIsWarning)
            return InitError(translatedErrorMessage);

        InitWarning(translatedErrorMessage);
        if(settings.GetArg("-dbloadfailexit",false))
            return false;
    }

    LogPrintf(" wallet      %15dms\n", GetTimeMillis() - nStart);
    ScanBlockchainForWalletUpdates();
    if(initializeBackendSettings)
        InitializeWalletBackendSettings(multiWalletModule->getWalletDbEnpointFactory());

    return true;
}

void InitializeMainBlockchainModules()
{
    const auto cacheSizes = CalculateDBCacheSizes();
    const bool unitTestMode = Params().NetworkID() == CBaseChainParams::UNITTEST;
    chainstateInstance.reset(
        new ChainstateManager (
            unitTestMode? (1 << 20) : cacheSizes.nBlockTreeDBCache,
            unitTestMode? (1 << 23) : cacheSizes.nCoinDBCache,
            unitTestMode? (  5000 ) : cacheSizes.nCoinCacheSize,
            unitTestMode?      true : false,
            unitTestMode?     false : settings.isReindexingBlocks()));
    sporkManagerInstance.reset(new CSporkManager(*chainstateInstance));
    InitializeMultiWalletModule();
    InitializeChainExtensionModule(GetMasternodeModule());
}
void FinalizeMainBlockchainModules()
{
    FinalizeChainExtensionModule();
    FinalizeMultiWalletModule();
    sporkManagerInstance.reset();
    chainstateInstance.reset();
}

bool InitializeDivi(boost::thread_group& threadGroup)
{
// ********************************************************* Step 1: setup
    globalThreadGroupRef = &threadGroup;
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
    InitializeTransactionDiskAccessors(GetTransactionMemoryPool(),cs_main);

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
    p2pNotifications.reset(new P2PNotifications());
    RegisterMainNotificationInterface(p2pNotifications.get());

    PruneHDSeedParameterInteraction();

#if ENABLE_ZMQ
    pzmqNotificationInterface = CZMQNotificationInterface::CreateWithArguments(settings);

    if (pzmqNotificationInterface) {
        RegisterMainNotificationInterface(pzmqNotificationInterface);
    }
#endif

    // ********************************************************* Step 7: load block chain
    CreateHardlinksForBlocks();

    uiInterface.InitMessage(translate("Preparing databases..."));
    InitializeMainBlockchainModules();

    const auto& chainActive = chainstateInstance->ActiveChain();
    const auto& blockMap = chainstateInstance->GetBlockMap();

    if(!SetSporkKey(*sporkManagerInstance))
        return false;

    bool fLoaded = false;
    int64_t nStart;
    while (!fLoaded) {
        bool fReset = settings.isReindexingBlocks();
        std::string strLoadError;

        uiInterface.InitMessage(translate("Loading block index..."));

        nStart = GetTimeMillis();
        const BlockLoadingStatus status = TryToLoadBlocks(*sporkManagerInstance, strLoadError);
        fLoaded = (status == BlockLoadingStatus::SUCCESS_LOADING);
        if(!fLoaded && status != BlockLoadingStatus::RETRY_LOADING)
        {
            return false;
        }
        if (!fLoaded) return InitError(strLoadError);
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
        LogPrintf("Wallet disabled!\n");
    } else {
        uiInterface.InitMessage(translate("Loading wallet..."));
        {
            CVerifyingNow chainVerificationInProgress(settings);
            if(!LoadAndSelectWallet(strWalletFile,false))
                return false;
            nStart = GetTimeMillis();
        }

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
    if (!GetChainExtensionService().updateActiveChain(state,nullptr))
        strErrors << "Failed to connect best block";

    InitializeBestHeaderBlockIndex();
#ifdef ENABLE_WALLET
    if(GetWallet() && settings.ParameterIsSet("-prunewalletconfs"))
    {
        GetWallet()->PruneWallet();
    }
#endif

    threadGroup.create_thread(boost::bind(&ReindexAndImportBlockFiles, chainstateInstance.get(), settings));

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
    if(!InitializeMasternodeIfRequested(settings, chainstateInstance->BlockTree().GetTxIndexing(), errorMessage))
    {
        return InitError(errorMessage);
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
    {
        auto pwallet = GetWallet();
        LogPrintf("Key Pool size = %u\n", pwallet ? pwallet->GetKeyPoolSize() : 0);
        LogPrintf("Address Book size = %u\n", pwallet ? pwallet->getAddressBookManager().getAddressBook().size() : 0);
    }
#endif

    if (settings.GetBoolArg("-listenonion", DEFAULT_LISTEN_ONION))
        StartTorControl(threadGroup);

    uiInterface.InitMessage(translate("Initializing P2P connections..."));
    StartNode(settings, cs_main, threadGroup);
#ifdef ENABLE_WALLET
    {
        auto pwallet = GetWallet();
        if (pwallet)
        {
            stakingWalletName = multiWalletModule->getActiveWalletName();
            StartCoinMintingModule(threadGroup,*pwallet);
        }
    }
#endif

    // ********************************************************* Step 12: finished

    SetRPCWarmupFinished();
    uiInterface.InitMessage(translate("Done loading"));

#ifdef ENABLE_WALLET
    auto pwallet = GetWallet();
    if (pwallet) {
        // Add wallet transactions that aren't already in a block to mapTransactions
        SubmitUnconfirmedWalletTransactionsToMempool(*pwallet);
        if(settings.ParameterIsSet("-prunewalletconfs"))
        {
            if(!pwallet->PruneWallet())
            {
                StartShutdown();
                LogPrintf("Failed to prune wallet correctly!");
                return false;
            }
        }
        // Run a thread to flush wallet periodically
        InitializeWalletBackendSettings(multiWalletModule->getWalletDbEnpointFactory());
    }
#endif

    return !fRequestShutdown;
}
