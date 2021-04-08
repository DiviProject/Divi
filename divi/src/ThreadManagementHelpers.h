#ifndef THREAD_MANAGEMENT_HELPERS_H
#define THREAD_MANAGEMENT_HELPERS_H
#if defined(HAVE_CONFIG_H)
#include "config/divi-config.h"
#endif
#include "compat.h"
#include <stdint.h>

#include <Logging.h>
#include <string>
#include <exception>
#include <utiltime.h>

#include <boost/filesystem/path.hpp>
#include <boost/thread/exceptions.hpp>

extern std::string strMiscWarning;
void PrintExceptionContinue(std::exception* pex, const char* pszThread);
void SetThreadPriority(int nPriority);
void RenameThread(const char* name);

/**
 * Standard wrapper for do-something-forever thread functions.
 * "Forever" really means until the thread is interrupted.
 * Use it like:
 *   new boost::thread(boost::bind(&LoopForever<void (*)()>, "dumpaddr", &DumpAddresses, 900000));
 * or maybe:
 *    boost::function<void()> f = boost::bind(&FunctionWithArg, argument);
 *    threadGroup.create_thread(boost::bind(&LoopForever<boost::function<void()> >, "nothing", f, milliseconds));
 */
template <typename Callable>
void LoopForever(const char* name, Callable func, int64_t msecs)
{
    std::string s = strprintf("divi-%s", name);
    RenameThread(s.c_str());
    LogPrintf("%s thread start\n", name);
    try {
        while (1) {
            MilliSleep(msecs);
            func();
        }
    } catch (boost::thread_interrupted) {
        LogPrintf("%s thread stop\n", name);
        throw;
    } catch (std::exception& e) {
        PrintExceptionContinue(&e, name);
        throw;
    } catch (...) {
        PrintExceptionContinue(NULL, name);
        throw;
    }
}

/**
 * .. and a wrapper that just calls func once
 */
template <typename Callable>
void TraceThread(const char* name, Callable func)
{
    std::string s = strprintf("divi-%s", name);
    RenameThread(s.c_str());
    try {
        LogPrintf("%s thread start\n", name);
        func();
        LogPrintf("%s thread exit\n", name);
    } catch (boost::thread_interrupted) {
        LogPrintf("%s thread interrupt\n", name);
        throw;
    } catch (std::exception& e) {
        PrintExceptionContinue(&e, name);
        throw;
    } catch (...) {
        PrintExceptionContinue(NULL, name);
        throw;
    }
}
template <typename Callable, typename Arg>
void TraceThread(const char* name, Callable func, Arg arg)
{
    auto compactFunction = [&arg,&func](){ func(arg); };
    TraceThread(name,compactFunction);
}

#endif //THREAD_MANAGEMENT_HELPERS_H