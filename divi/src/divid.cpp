// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "clientversion.h"
#include <DataDirectory.h>
#include "init.h"
#include <chainparams.h>
#include "noui.h"
#include "ui_interface.h"
#include "util.h"
#include "Settings.h"
#include <ThreadManagementHelpers.h>

#include <LicenseAndInfo.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

/* Introduction text for doxygen: */

/*! \mainpage Developer documentation
 *
 * \section intro_sec Introduction
 *
 * This is the developer documentation of the reference client for an experimental new digital currency called DIVI (http://www.diviproject.org),
 * which enables instant payments to anyone, anywhere in the world. DIVI uses peer-to-peer technology to operate
 * with no central authority: managing transactions and issuing money are carried out collectively by the network.
 *
 * The software is a community-driven open source project, released under the MIT license.
 *
 * \section Navigation
 * Use the buttons <code>Namespaces</code>, <code>Classes</code> or <code>Files</code> at the top of the page to start navigating the code.
 */

extern Settings& settings;

void DetectShutdownThread(boost::thread_group* threadGroup)
{
    bool fShutdown = ShutdownRequested();
    // Tell the main threads to shutdown.
    while (!fShutdown) {
        MilliSleep(200);
        fShutdown = ShutdownRequested();
    }
    if (threadGroup) {
        threadGroup->interrupt_all();
        threadGroup->join_all();
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
bool AppInit(int argc, char* argv[])
{
    boost::thread_group threadGroup;
    boost::thread* detectShutdownThread = NULL;

    bool fRet = false;

    //
    // Parameters
    //
    // If Qt is used, parameters/divi.conf are parsed in qt/divi.cpp's main()
    settings.ParseParameters(argc, argv);
    EnableMainSignals();
    // Process help and version before taking care about datadir
    if (settings.ParameterIsSet("-?") || settings.ParameterIsSet("-help") || settings.ParameterIsSet("-version")) {
        std::string strUsage = translate("Divi Core Daemon") + " " + translate("version") + " " + FormatFullVersion() + "\n";

        if (settings.ParameterIsSet("-version")) {
            strUsage += LicenseInfo();
        } else {
            strUsage += "\n" + translate("Usage:") + "\n" +
                        "  divid [options]                     " + translate("Start Divi Core Daemon") + "\n";

            strUsage += "\n" + HelpMessage(HMM_BITCOIND,settings,Params(CBaseChainParams::MAIN));
        }

        fprintf(stdout, "%s", strUsage.c_str());
        return false;
    }

    try {
        if (!boost::filesystem::is_directory(GetDataDir(false))) {
            fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", settings.GetParameter("-datadir").c_str());
            return false;
        }
        try {
            settings.ReadConfigFile();
        } catch (std::exception& e) {
            fprintf(stderr, "Error reading configuration file: %s\n", e.what());
            return false;
        }
        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        if (!SelectParamsFromCommandLine(settings)) {
            fprintf(stderr, "Error: Invalid combination of -regtest and -testnet.\n");
            return false;
        }

        // Command-line RPC
        bool fCommandLine = false;
        for (int i = 1; i < argc; i++)
            if (!IsSwitchChar(argv[i][0]) && !boost::algorithm::istarts_with(argv[i], "divi:"))
                fCommandLine = true;

        if (fCommandLine) {
            fprintf(stderr, "Error: There is no RPC client functionality in divid anymore. Use the divi-cli utility instead.\n");
            exit(1);
        }
#ifndef WIN32
        if (settings.GetBoolArg("-daemon", false))
        {
            fprintf(stdout, "DIVI server starting\n");

            // Daemonize
            pid_t pid = fork();
            if (pid < 0) {
                fprintf(stderr, "Error: fork() returned %d errno %d\n", pid, errno);
                return false;
            }
            if (pid > 0) // Parent process, pid is child process id
            {
                return true;
            }
            // Child process falls through to rest of initialization

            pid_t sid = setsid();
            if (sid < 0)
                fprintf(stderr, "Error: setsid() returned %d errno %d\n", sid, errno);
        }
#endif
        settings.SoftSetBoolArg("-server", true);
        if(settings.ParameterIsSet("-mocktime"))
        {
            SetMockTime(settings.GetArg("-mocktime",GetTime()));
        }

        detectShutdownThread = new boost::thread(boost::bind(&DetectShutdownThread, &threadGroup));
        fRet = InitializeDivi(threadGroup);
    } catch (std::exception& e) {
        PrintExceptionContinue(&e, "AppInit()");
    } catch (...) {
        PrintExceptionContinue(NULL, "AppInit()");
    }

    if (!fRet) {
        if (detectShutdownThread)
            detectShutdownThread->interrupt();

        threadGroup.interrupt_all();
        // threadGroup.join_all(); was left out intentionally here, because we didn't re-test all of
        // the startup-failure cases to make sure they don't result in a hang due to some
        // thread-blocking-waiting-for-another-thread-during-startup case
    }

    if (detectShutdownThread) {
        detectShutdownThread->join();
        delete detectShutdownThread;
        detectShutdownThread = NULL;
    }
    Shutdown();

    return fRet;
}

int main(int argc, char* argv[])
{
    SetupEnvironment();

    // Connect divid signal handlers
    noui_connect();

    return (AppInit(argc, argv) ? 0 : 1);
}
