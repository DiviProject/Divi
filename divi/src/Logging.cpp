#include <Logging.h>

#include <stdio.h>
#include <boost/filesystem/path.hpp>
#include <boost/thread.hpp>
#include <utiltime.h>
#include <set>

#include <DataDirectory.h>
#include <chainparamsbase.h>

bool fDebug = false;
bool fPrintToConsole = false;
bool fPrintToDebugLog = true;
volatile bool fReopenDebugLog = false;
bool fLogTimestamps = false;

// depends on multiArgs - rather the debug flags passed I should say

static boost::once_flag debugPrintInitFlag = BOOST_ONCE_INIT;
/**
 * We use boost::call_once() to make sure these are initialized
 * in a thread-safe manner the first time called:
 */
static FILE* fileout = NULL;
static boost::mutex* mutexDebugLog = NULL;

boost::filesystem::path getDebugLogPath()
{
    return GetDataDir() / "debug.log";
}

static void DebugPrintInit()
{
    assert(fileout == NULL);
    assert(mutexDebugLog == NULL);

    boost::filesystem::path pathDebug = getDebugLogPath();
    fileout = fopen(pathDebug.string().c_str(), "a");
    if (fileout) setbuf(fileout, NULL); // unbuffered

    mutexDebugLog = new boost::mutex();
}

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
            const std::vector<std::string>& categories = mapMultiArgs["-debug"];
            ptrCategory.reset(new std::set<std::string>(categories.begin(), categories.end()));
            // thread_specific_ptr automatically deletes the set when the thread ends.
            // "divi" is a composite category enabling all DIVI-related debug output
            if (ptrCategory->count(std::string("divi"))) {
                ptrCategory->insert(std::string("obfuscation"));
                ptrCategory->insert(std::string("swiftx"));
                ptrCategory->insert(std::string("masternode"));
                ptrCategory->insert(std::string("mnpayments"));
                ptrCategory->insert(std::string("staking"));
//                ptrCategory->insert(string("zero"));
                ptrCategory->insert(std::string("mnbudget"));
            }
        }
        const std::set<std::string>& setCategories = *ptrCategory.get();

        // if not debugging everything and not debugging specific category, LogPrint does nothing.
        if (setCategories.count(std::string("")) == 0 &&
            setCategories.count(std::string(category)) == 0)
            return false;
    }
    return true;
}

int LogPrintStr(const std::string& str)
{
	int ret = 0; // Returns total number of characters written
	//ret = fwrite(str.data(), 1, str.size(), stdout);
	//fflush(stdout);


    if (fPrintToConsole) {
        // print to console
        ret = fwrite(str.data(), 1, str.size(), stdout);
        fflush(stdout);
    } else if (fPrintToDebugLog && AreBaseParamsConfigured()) {
        static bool fStartedNewLine = true;
        boost::call_once(&DebugPrintInit, debugPrintInitFlag);

        if (fileout == NULL)
            return ret;

        boost::mutex::scoped_lock scoped_lock(*mutexDebugLog);

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