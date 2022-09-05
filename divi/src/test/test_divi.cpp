// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define BOOST_TEST_MODULE Divi Test Suite

#include <dbenv.h>
#include <chain.h>
#include <ChainstateManager.h>
#include <init.h>
#include "main.h"
#include "net.h"
#include "spork.h"
#include "random.h"
#include "txdb.h"
#include "ui_interface.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "db.h"
#include "wallet.h"
#endif
#include <MasternodeModule.h>
#include <TransactionInputChecker.h>
#include <chainparams.h>
#include <Settings.h>
#include <I_Clock.h>

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread.hpp>

extern CClientUIInterface uiInterface;
extern Settings& settings;
extern void noui_connect();

/* The global spork-manager instance is normally managed by init.cpp (where it
   is also defined).  For unit tests, we override the init sequence, and
   manually construct / destruct the instance.  Since code depends on
   GetSporkManager() to retrieve the global instance, need to actually use
   that one rather than e.g. an instance in the test fixture.

   FIXME: Get rid of GetSporkManager(), pass the instance everywhere, and then
   make the instance part of the test class.  */
extern std::unique_ptr<CSporkManager> sporkManagerInstance;

class TestLocalClock final: public I_Clock
{
public:
    virtual int64_t getTime() const
    {
        return 0;
    }
};

struct TestingSetup {
    boost::filesystem::path pathTemp;
    boost::thread_group threadGroup;
    CDBEnv& bitdb_;

    std::unique_ptr<ChainstateManager> chainstateInstance;
    std::unique_ptr<TestLocalClock> localClock;
    std::unique_ptr<MasternodeModule> mnModule;

    TestingSetup(): pathTemp(), threadGroup(), bitdb_(BerkleyDBEnvWrapper())
    {
        SetupEnvironment();
        setWriteToDebugLogFlag(false);
        settings.SetParameter("-checkblockindex","1");
        SelectParams(CBaseChainParams::UNITTEST);
        noui_connect();
#ifdef ENABLE_WALLET
        bitdb_.MakeMock();
#endif
        pathTemp = GetTempPath() / strprintf("test_divi_%lu_%i", (unsigned long)GetTime(), (int)(GetRand(100000)));
        boost::filesystem::create_directories(pathTemp);
        settings.SetParameter("-datadir", pathTemp.string());
        chainstateInstance.reset(new ChainstateManager(1 << 20, 1 << 23, 5000, true, false));
        sporkManagerInstance.reset(new CSporkManager(*chainstateInstance));
        localClock.reset(new TestLocalClock());
        mnModule.reset( new MasternodeModule(*localClock, GetPeerSyncQueryService(), *chainstateInstance, GetNetworkAddressManager()) );
        InitializeChainExtensionService(*mnModule);
        InitializeMultiWalletModule();
        ConnectGenesisBlock(*chainstateInstance);
        TransactionInputChecker::SetScriptCheckingThreadCount(3);
        TransactionInputChecker::InitializeScriptCheckingThreads(threadGroup);
        RegisterNodeSignals(GetNodeSignals());
        EnableUnitTestSignals();
    }
    ~TestingSetup()
    {
        threadGroup.interrupt_all();
        threadGroup.join_all();
        UnregisterNodeSignals(GetNodeSignals());
        FinalizeMultiWalletModule();
        FinalizeChainExtensionService();
        mnModule.reset();
        localClock.reset();
        sporkManagerInstance.reset();
        chainstateInstance.reset();
#ifdef ENABLE_WALLET
        bitdb_.Flush(true);
#endif
        boost::filesystem::remove_all(pathTemp);
    }
};

BOOST_GLOBAL_FIXTURE(TestingSetup);
