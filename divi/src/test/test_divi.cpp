// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define BOOST_TEST_MODULE Divi Test Suite

#include <chainparams.h>
#include <dbenv.h>
#include <init.h>
#include <I_ChainExtensionService.h>
#include <Logging.h>
#include <noui.h>
#include <random.h>
#include <Settings.h>
#include <tinyformat.h>
#include <TransactionInputChecker.h>
#include <util.h>
#include <utiltime.h>

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread.hpp>

extern Settings& settings;

/* The global spork-manager instance is normally managed by init.cpp (where it
   is also defined).  For unit tests, we override the init sequence, and
   manually construct / destruct the instance.  Since code depends on
   GetSporkManager() to retrieve the global instance, need to actually use
   that one rather than e.g. an instance in the test fixture.

   FIXME: Get rid of GetSporkManager(), pass the instance everywhere, and then
   make the instance part of the test class.  */
extern void RegisterNodeSignals();
extern void UnregisterNodeSignals();

struct TestingSetup {
    boost::filesystem::path pathTemp;
    boost::thread_group threadGroup;
    CDBEnv& bitdb_;

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
        InitializeMainBlockchainModules();
        InitializeMultiWalletModule();
        GetChainExtensionService().connectGenesisBlock();
        TransactionInputChecker::SetScriptCheckingThreadCount(3);
        TransactionInputChecker::InitializeScriptCheckingThreads(threadGroup);
        RegisterNodeSignals();
        EnableUnitTestSignals();
    }
    ~TestingSetup()
    {
        threadGroup.interrupt_all();
        threadGroup.join_all();
        UnregisterNodeSignals();
        FinalizeMultiWalletModule();
        FinalizeMainBlockchainModules();
#ifdef ENABLE_WALLET
        bitdb_.Flush(true);
#endif
        boost::filesystem::remove_all(pathTemp);
    }
};

BOOST_GLOBAL_FIXTURE(TestingSetup);
