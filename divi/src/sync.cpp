// Copyright (c) 2011-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sync.h"

#include "Logging.h"
#include "utilstrencodings.h"

#include <stdio.h>

#include <boost/foreach.hpp>
#include <boost/thread.hpp>

#include <sstream>

#ifdef DEBUG_LOCKCONTENTION
void PrintLockContention(const char* pszName, const char* pszFile, int nLine)
{
    LogPrintf("LOCKCONTENTION: %s\n", pszName);
    LogPrintf("Locker: %s:%d\n", pszFile, nLine);
}
#endif /* DEBUG_LOCKCONTENTION */

#ifdef DEBUG_LOCKORDER
//
// Early deadlock detection.
// Problem being solved:
//    Thread 1 locks  A, then B, then C
//    Thread 2 locks  D, then C, then A
//     --> may result in deadlock between the two threads, depending on when they run.
// Solution implemented here:
// Keep track of pairs of locks: (A before B), (A before C), etc.
// Complain if any thread tries to lock in a different order.
//

struct CLockLocation {
    CLockLocation(const char* pszName, const char* pszFile, int nLine)
    {
        mutexName = pszName;
        sourceFile = pszFile;
        sourceLine = nLine;
    }

    std::string ToString() const
    {
        return mutexName + "  " + sourceFile + ":" + itostr(sourceLine);
    }

    std::string MutexName() const { return mutexName; }

private:
    std::string mutexName;
    std::string sourceFile;
    int sourceLine;
};

LOG_FORMAT_WITH_TOSTRING(CLockLocation)

typedef std::vector<std::pair<void*, CLockLocation> > LockStack;

static boost::mutex dd_mutex;
static std::map<std::pair<void*, void*>, LockStack> lockorders;
static boost::thread_specific_ptr<LockStack> lockstack;


static void potential_deadlock_detected(const std::pair<void*, void*>& mismatch, const LockStack& s1, const LockStack& s2)
{
    LogPrintf("POTENTIAL DEADLOCK DETECTED\n");
    LogPrintf("Previous lock order was:\n");

    std::string lockOrderData = "Prior Order:\n";
    std::string firstMismatch = "";
    std::string secondMismatch = "";
    fprintf(stderr, "POTENTIAL DEADLOCK DETECTED\n");

    std::string priorLockStackState = "";
    BOOST_FOREACH (const PAIRTYPE(void*, CLockLocation) & lockIdAndLocation, s2) {
        if (lockIdAndLocation.first == mismatch.first)
        {
            lockOrderData += std::string("\tFirst: ") +lockIdAndLocation.second.ToString() + "\n";
        }
        if (lockIdAndLocation.first == mismatch.second)
        {
            lockOrderData += std::string("\tSecond: ") +lockIdAndLocation.second.ToString() + "\n";
        }
        priorLockStackState += lockIdAndLocation.second.ToString() + " |";
        LogPrintf(" %s\n", lockIdAndLocation.second);
    }
    lockOrderData += std::string("\n")+priorLockStackState;

    std::string currentLockStackState = "";
    lockOrderData += "\n\nCurrent Order:\n";
    LogPrintf("Current lock order is:\n");
    BOOST_FOREACH (const PAIRTYPE(void*, CLockLocation) & lockIdAndLocation, s1) {
        if (lockIdAndLocation.first == mismatch.first)
        {
            lockOrderData += std::string("\tFirst: ") +lockIdAndLocation.second.ToString() + "\n";
        }
        if (lockIdAndLocation.first == mismatch.second)
        {
            lockOrderData += std::string("\tSecond: ") +lockIdAndLocation.second.ToString() + "\n";
        }
        currentLockStackState += lockIdAndLocation.second.ToString() + " |";
        LogPrintf(" %s\n", lockIdAndLocation.second);
    }
    lockOrderData+=std::string("\n")+currentLockStackState;

    tfm::format(std::cerr, "Assertion failed: detected inconsistent lock order for %s, details in debug log.\n%s", s2.back().second.ToString(),lockOrderData.c_str());
    abort();
}

static void push_lock(void* c, const CLockLocation& locklocation, bool fTry)
{
    if (lockstack.get() == NULL)
        lockstack.reset(new LockStack);

    LogPrint("lock", "Locking: %s\n", locklocation);
    dd_mutex.lock();

    (*lockstack).push_back(std::make_pair(c, locklocation));

    if (!fTry) {
        BOOST_FOREACH (const PAIRTYPE(void*, CLockLocation) & i, (*lockstack)) {
            if (i.first == c)
                break;

            std::pair<void*, void*> p1 = std::make_pair(i.first, c);
            if (lockorders.count(p1))
                continue;
            lockorders[p1] = (*lockstack);

            std::pair<void*, void*> p2 = std::make_pair(c, i.first);
            if (lockorders.count(p2)) {
                potential_deadlock_detected(p1, lockorders[p2], lockorders[p1]);
                break;
            }
        }
    }
    dd_mutex.unlock();
}

static void pop_lock()
{
    if (fDebug) {
        const CLockLocation& locklocation = (*lockstack).rbegin()->second;
        LogPrint("lock", "Unlocked: %s\n", locklocation);
    }
    dd_mutex.lock();
    (*lockstack).pop_back();
    dd_mutex.unlock();
}

void EnterCritical(const char* pszName, const char* pszFile, int nLine, void* cs, bool fTry)
{
    push_lock(cs, CLockLocation(pszName, pszFile, nLine), fTry);
}

void LeaveCritical()
{
    pop_lock();
}

std::string LocksHeld()
{
    std::string result;
    BOOST_FOREACH (const PAIRTYPE(void*, CLockLocation) & i, *lockstack)
    {
        std::ostringstream ss;
        ss << (uint64_t)i.first;
        result +=std::string("\t")+ss.str()+"|"+ i.second.ToString() + std::string("\n");
    }

    return result;
}

void AssertLockHeldInternal(const char* pszName, const char* pszFile, int nLine, void* cs)
{
    BOOST_FOREACH (const PAIRTYPE(void*, CLockLocation) & i, *lockstack)
        if (i.first == cs)
            return;
    std::string errorMessage = LocksHeld();
    fprintf(stderr, "Assertion failed: lock %s not held in %s:%i; locks held:\n%s", pszName, pszFile, nLine, errorMessage.empty()? "No locks held": errorMessage.c_str());
    abort();
}

#endif /* DEBUG_LOCKORDER */
