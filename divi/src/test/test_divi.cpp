// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define BOOST_TEST_MODULE Divi Test Suite
#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <dbenv.h>
#include <init.h>
#include <main.h>

struct TestingSetup {
    CDBEnv& berkleyEnvironment_;
    TestingSetup(): berkleyEnvironment_(BerkleyDBEnvWrapper())
    {
#ifdef ENABLE_WALLET
        berkleyEnvironment_.MakeMock();
#endif
        SelectParams(CBaseChainParams::UNITTEST);
        InitializeMainBlockchainModules();
        InitializeMultiWalletModule();
        RegisterNodeSignals();
        EnableUnitTestSignals();
    }
    ~TestingSetup()
    {
        UnregisterNodeSignals();
        FinalizeMultiWalletModule();
        FinalizeMainBlockchainModules();
#ifdef ENABLE_WALLET
        berkleyEnvironment_.Flush(true);
#endif
    }
};

BOOST_GLOBAL_FIXTURE(TestingSetup);
