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
    CLockLocation(const char* pszName, const char* pszFile, int nLine, bool isTryLock)
    {
        mutexName = pszName;
        sourceFile = pszFile;
        sourceLine = nLine;
        tryLock = isTryLock;
    }

    std::string ToString() const
    {
        return mutexName + "  " + sourceFile + ":" + itostr(sourceLine) +"[TryLock: "+std::to_string(tryLock)+"]";
    }

    std::string MutexName() const { return mutexName; }
    bool isTry() const {return tryLock;}
private:
    std::string mutexName;
    std::string sourceFile;
    int sourceLine;
    bool tryLock;
};

LOG_FORMAT_WITH_TOSTRING(CLockLocation)

//typedef std::vector<std::pair<MutexId, CLockLocation> > LockStack;
struct LockStack
{
private:
    std::vector<std::pair<MutexId, CLockLocation> > stack_;
    unsigned id_;
public:
    const std::vector<std::pair<MutexId, CLockLocation> >& stack() const
    {
        return stack_;
    }
    unsigned id() const
    {
        return id_;
    }
    void push_back(std::pair<MutexId, CLockLocation>&& pair)
    {
        stack_.push_back(pair);
    }
    void pop_back()
    {
        stack_.pop_back();
    }

    LockStack(): stack_(), id_(0u) {}
    LockStack(const unsigned id): stack_(), id_(id) {}
    LockStack& operator=(const LockStack& other)
    {
        stack_ = other.stack();
        id_ = other.id();
        return *this;
    }
};

typedef std::pair<MutexId,MutexId> LockOrderID;

static boost::mutex dd_mutex;
static std::map<LockOrderID, LockStack> lockorders;
static boost::thread_specific_ptr<LockStack> lockstack;
static boost::thread_specific_ptr<std::vector<LockOrderID>> tryLockQueue;

static void potential_deadlock_detected(const std::pair<MutexId, MutexId>& mismatch, const LockStack& s1, const LockStack& s2)
{
    LogPrintf("POTENTIAL DEADLOCK DETECTED\n");
    LogPrintf("Previous lock order was:\n");

    std::string lockOrderData = "Prior Order:\n";
    std::string firstMismatch = "";
    std::string secondMismatch = "";
    fprintf(stderr, "POTENTIAL DEADLOCK DETECTED\n");

    std::string priorLockStackState = "";
    BOOST_FOREACH (const PAIRTYPE(MutexId, CLockLocation) & lockIdAndLocation, s2.stack()) {
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
    BOOST_FOREACH (const PAIRTYPE(MutexId, CLockLocation) & lockIdAndLocation, s1.stack()) {
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
    lockOrderData+=std::string("\n")+currentLockStackState+std::string("\n");

    tfm::format(std::cerr, "Assertion failed: detected inconsistent lock order for %s, details in debug log.\n%s", s2.stack().back().second.ToString(),lockOrderData.c_str());
    abort();
}

static void push_lock(MutexId c, const CLockLocation& locklocation)
{
    static unsigned lockStackID = 0;
    if (lockstack.get() == NULL)
        lockstack.reset(new LockStack(++lockStackID));
    if(tryLockQueue.get() == NULL)
        tryLockQueue.reset( new std::vector<LockOrderID>);

    (*lockstack).push_back(std::make_pair(c, locklocation));

    BOOST_FOREACH (const PAIRTYPE(MutexId, CLockLocation) & i, (*lockstack).stack())
    {
        if (i.first == c)
            break;

        std::pair<MutexId, MutexId> p1 = std::make_pair(i.first, c);
        if (lockorders.count(p1))
            continue;
        if (!locklocation.isTry())
        {
            lockorders[p1] = (*lockstack);

            std::pair<MutexId, MutexId> p2 = std::make_pair(c, i.first);
            if (lockorders.count(p2)) {
                const LockStack& currentLockStack = lockorders[p1];
                const LockStack& priorLockStack = lockorders[p2];
                if(currentLockStack.id() != priorLockStack.id())
                {
                const CLockLocation& currentLock = currentLockStack.stack().back().second;
                const CLockLocation& priorLock = priorLockStack.stack().back().second;
                if(!(currentLock.isTry() || priorLock.isTry()))
                {
                    potential_deadlock_detected(p1, lockorders[p1], lockorders[p2]);
                    break;
                }
                }
            }
        }
        else
        {
            tryLockQueue->push_back(p1);
        }
    }
}


static void pop_lock()
{
    (*lockstack).pop_back();
}

void EnterCritical(const char* pszName, const char* pszFile, int nLine, MutexId cs, bool fTry)
{
    CLockLocation locklocation(pszName, pszFile, nLine,fTry);
    LogPrint("lock", "Locking: %s\n", locklocation);
    dd_mutex.lock();
    push_lock(cs, locklocation);
    if(!fTry) dd_mutex.unlock();
}
void ConfirmCritical()
{
    for(LockOrderID id: *tryLockQueue)
    {
        if (lockorders.count(id)>0)
            continue;
        lockorders[id] = (*lockstack);
    }
    tryLockQueue->clear();
    dd_mutex.unlock();
}
void LeaveCritical(bool fTry)
{
    LogPrint("lock", "Unlocked: %s\n", (*lockstack).stack().rbegin()->second);
    if(!fTry) dd_mutex.lock();
    tryLockQueue->clear();
    pop_lock();
    dd_mutex.unlock();
}

std::string LocksHeld()
{
    std::string result;
    BOOST_FOREACH (const PAIRTYPE(MutexId, CLockLocation) & i, (*lockstack).stack())
    {
        std::ostringstream ss;
        ss << (uint64_t)i.first;
        result +=std::string("\t")+ss.str()+"|"+ i.second.ToString() + std::string("\n");
    }

    return result;
}

void AssertLockHeldInternal(const char* pszName, const char* pszFile, int nLine, MutexId cs)
{
    BOOST_FOREACH (const PAIRTYPE(MutexId, CLockLocation) & i, (*lockstack).stack())
        if (i.first == cs)
            return;
    std::string errorMessage = LocksHeld();
    fprintf(stderr, "Assertion failed: lock %s not held in %s:%i; locks held:\n%s", pszName, pszFile, nLine, errorMessage.empty()? "No locks held": errorMessage.c_str());
    abort();
}
void RegisterMutexId(unsigned& mutexId)
{
    static boost::mutex idMutex;
    static unsigned nextMutexId = 1u;
    idMutex.lock();
    mutexId = nextMutexId++;
    idMutex.unlock();
}
#endif /* DEBUG_LOCKORDER */
