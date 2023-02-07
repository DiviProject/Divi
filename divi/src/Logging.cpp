#include <Logging.h>

#include <stdio.h>
#include <boost/filesystem/path.hpp>
#include <boost/thread.hpp>
#include <utiltime.h>
#include <mutex>
#include <set>

#include <DataDirectory.h>
#include <chainparamsbase.h>
#include <uint256.h>
#include <serialize.h>
#include <Settings.h>

volatile bool fDebug = false;
bool fPrintToConsole = false;
volatile bool fPrintToDebugLog = true;
volatile bool fReopenDebugLog = false;
volatile bool fLogTimestamps = false;
bool fLogIPs = false;

extern Settings& settings;

void requestReopeningDebugLog()
{
    fReopenDebugLog = true;
}
void setWriteToDebugLogFlag(bool settingValue)
{
    fPrintToDebugLog = settingValue;
}

LOG_FORMAT_WITH_TOSTRING(uint256)

namespace
{

FILE* fileout = nullptr;

boost::filesystem::path getDebugLogPath()
{
    return GetDataDir() / "debug.log";
}

/** Ensures that the logging system has been initialised once.  Returns
 *  true if either it is initialised now or was already before.  In case
 *  there is a recursive call (i.e. logging performed from inside the
 *  initialisation logic), it returns false to avoid a deadlock.  */
bool EnsureDebugPrintInitialized()
{
    /* Once all is initialised, we can avoid even the mutex lock below
       to avoid potential mutex contention.  For this, we use a flag
       to bypass the rest of the logic.

       This is not synchronised here, but it only flips from false to true
       once and then stays true.  And even if a thread "misses" the flip,
       the worst that happens is that it then runs into the mutex lock
       and there gets a memory barrier synchronisation.  */
    static bool initialised = false;
    if (initialised)
        return true;

    static std::recursive_mutex mut;
    static bool inProgress = false;

    std::lock_guard<std::recursive_mutex> lock(mut);

    /* It may have happened that initialisation was still going on when the
       call started, then waited on the mutex, and now that we got the lock,
       is already done.  */
    if (initialised)
        return true;

    if (inProgress)
        return false;

    inProgress = true;

    assert(fileout == NULL);

    boost::filesystem::path pathDebug = getDebugLogPath();
    fileout = fopen(pathDebug.string().c_str(), "a");
    if (fileout) setbuf(fileout, NULL); // unbuffered

    inProgress = false;
    initialised = true;

    return true;
}

} // anonymous namespace

bool LogAcceptCategory(const char* category)
{
    if (category != NULL) {
        if (!fDebug)
            return false;

        // Give each thread quick access to -debug settings.
        // This helps prevent issues debugging global destructors,
        // where mapMultiArgs might be deleted before another
        // global destructor calls LogPrint()
        static boost::thread_specific_ptr< std::set<std::string> > ptrCategory;
        if (ptrCategory.get() == NULL) {
            const std::vector<std::string> categories = settings.GetMultiParameter("-debug");
            ptrCategory.reset(new std::set<std::string>(categories.begin(), categories.end()));
            // thread_specific_ptr automatically deletes the set when the thread ends.
        }
        const std::set<std::string>& setCategories = *ptrCategory.get();

        // "all" is a composite category enabling all DIVI-related debug output
        if (ptrCategory->count(std::string("all")) || ptrCategory->count(std::string("")))
            return true;

        // if not debugging specific category, LogPrint does nothing.
        return setCategories.count(std::string(category)) > 0;
    }
    return true;
}

int LogPrintStr(const std::string& str)
{
    int ret = 0; // Returns total number of characters written
    //ret = fwrite(str.data(), 1, str.size(), stdout);
    //fflush(stdout);

    const bool initFailed = !fPrintToConsole && !EnsureDebugPrintInitialized();

    if (fPrintToConsole || initFailed) {
        // print to console
        ret = fwrite(str.data(), 1, str.size(), stdout);
        fflush(stdout);
    } else if (fPrintToDebugLog && AreBaseParamsConfigured()) {
        static bool fStartedNewLine = true;
        static std::mutex mutexDebugLog;

        if (fileout == NULL)
            return ret;

        std::lock_guard<std::mutex> scoped_lock(mutexDebugLog);

        // reopen the log file, if requested
        if (fReopenDebugLog) {
            fReopenDebugLog = false;
            boost::filesystem::path pathDebug = getDebugLogPath();
            if (freopen(pathDebug.string().c_str(), "a", fileout) != NULL)
                setbuf(fileout, NULL); // unbuffered
        }

        // Debug print useful for profiling
        if (fLogTimestamps && fStartedNewLine)
            ret += fprintf(fileout, "%s ", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()).c_str());
        if (!str.empty() && str[str.size() - 1] == '\n')
            fStartedNewLine = true;
        else
            fStartedNewLine = false;

        ret = fwrite(str.data(), 1, str.size(), fileout);
    }

    return ret;
}

void ShrinkDebugFile()
{
    // Scroll debug.log if it's getting too big
    static boost::filesystem::path pathLog = GetDataDir() / "debug.log";
    FILE* file = fopen(pathLog.string().c_str(), "r");
    if (file && boost::filesystem::file_size(pathLog) > 10 * 1000000) {
        // Restart the file with some of the end
        std::vector<char> vch(200000, 0);
        fseek(file, -((long)vch.size()), SEEK_END);
        int nBytes = fread(begin_ptr(vch), 1, vch.size(), file);
        fclose(file);

        file = fopen(pathLog.string().c_str(), "w");
        if (file) {
            fwrite(begin_ptr(vch), 1, nBytes, file);
            fclose(file);
        }
    } else if (file != NULL)
        fclose(file);
}

void SetLoggingAndDebugSettings()
{
    fPrintToConsole = settings.GetBoolArg("-printtoconsole", false);
    fLogTimestamps = settings.GetBoolArg("-logtimestamps", true);
    fLogIPs = settings.GetBoolArg("-logips", false);

    fDebug = settings.debugModeIsEnabled();

    if (settings.GetBoolArg("-shrinkdebugfile", !fDebug))
        ShrinkDebugFile();

    if(fPrintToConsole)
    {
        setvbuf(stdout, NULL, _IOLBF, 0);
    }
}

bool ShouldLogPeerIPs()
{
    return fLogIPs;
}
bool ShouldLogTimestamps()
{
    return fLogTimestamps;
}
