// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/divi-config.h"
#endif

#include "util.h"

#include "allocators.h"
#include "chainparamsbase.h"
#include "random.h"
#include "serialize.h"
#include "sync.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include <Settings.h>
#include <stdarg.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/crypto.h> // for OPENSSL_cleanse()
#include <openssl/evp.h>


#ifndef WIN32
// for posix_fallocate
#ifdef __linux__

#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif

#define _POSIX_C_SOURCE 200112L

#endif // __linux__

#include <algorithm>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>

#else

#ifdef _MSC_VER
#pragma warning(disable : 4786)
#pragma warning(disable : 4804)
#pragma warning(disable : 4805)
#pragma warning(disable : 4717)
#endif

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0501

#ifdef _WIN32_IE
#undef _WIN32_IE
#endif
#define _WIN32_IE 0x0501

#define WIN32_LEAN_AND_MEAN 1
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <io.h> /* for _commit */
#include <shlobj.h>
#endif

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp> // for startswith() and endswith()
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options/detail/config_file.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/thread.hpp>
#include <openssl/conf.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>

// Work around clang compilation problem in Boost 1.46:
// /usr/include/boost/program_options/detail/config_file.hpp:163:17: error: call to function 'to_internal' that is neither visible in the template definition nor found by argument-dependent lookup
// See also: http://stackoverflow.com/questions/10020179/compilation-fail-in-boost-librairies-program-options
//           http://clang.debian.net/status.php?version=3.0&key=CANNOT_FIND_FUNCTION
namespace boost
{
namespace program_options
{
std::string to_internal(const std::string&);
}

} // namespace boost

using namespace std;

// DIVI only features

// Masternode
bool fMasterNode = false;
string strMasterNodePrivKey = "";
string strMasterNodeAddr = "";
bool fLiteMode = false;
// SwiftX
bool fEnableSwiftTX = true;
int nSwiftTXDepth = 5;

int nAnonymizeDiviAmount = 1000;
int nLiquidityProvider = 0;
/** Spork enforcement enabled time */
int64_t enforceMasternodePaymentsTime = 4085657524;
bool fSucessfullyLoaded = false;

map<string, string> mapArgs;
map<string, vector<string> > mapMultiArgs;
bool fDaemon = false;
bool fLogIPs = false;


/* PrintException */
string strMiscWarning;

static std::string FormatException(std::exception* pex, const char* pszThread)
{
#ifdef WIN32
    char pszModule[MAX_PATH] = "";
    GetModuleFileNameA(NULL, pszModule, sizeof(pszModule));
#else
    const char* pszModule = "divi";
#endif
    if (pex)
        return strprintf(
            "EXCEPTION: %s       \n%s       \n%s in %s       \n", typeid(*pex).name(), pex->what(), pszModule, pszThread);
    else
        return strprintf(
            "UNKNOWN EXCEPTION       \n%s in %s       \n", pszModule, pszThread);
}

void PrintExceptionContinue(std::exception* pex, const char* pszThread)
{
    std::string message = FormatException(pex, pszThread);
    LogPrintf("\n\n************************\n%s\n", message);
    fprintf(stderr, "\n\n************************\n%s\n", message.c_str());
    strMiscWarning = message;
}

Settings& settings = Settings::instance(mapArgs, mapMultiArgs);

/** Interpret string as boolean, for argument parsing */
static bool InterpretBool(const std::string& strValue)
{
    return settings.InterpretBool(strValue);
}

/** Turn -noX into -X=0 */
static void InterpretNegativeSetting(std::string& strKey, std::string& strValue)
{
    return settings.InterpretNegativeSetting(strKey, strValue);
}

void ParseParameters(int argc, const char* const argv[])
{
    settings.ParseParameters(argc, argv);
}

bool ParameterIsSet (const std::string& key)
{
    return settings.ParameterIsSet(key);
}

bool ParameterIsSetForMultiArgs (const std::string& key)
{
    return settings.ParameterIsSetForMultiArgs(key);
}

std::string GetParameter(const std::string& key)
{
    return settings.GetParameter(key);
}

void ForceRemoveArg(const std::string &strArg)
{
    settings.ForceRemoveArg(strArg);
}

void SetParameter (const std::string& key, const std::string& value)
{
    settings.SetParameter(key, value);
}

void ClearParameter () 
{
    settings.ClearParameter();
}

std::string GetArg(const std::string& strArg, const std::string& strDefault)
{
    return settings.GetArg(strArg, strDefault);
}

int64_t GetArg(const std::string& strArg, int64_t nDefault)
{
    return settings.GetArg(strArg, nDefault);
}

bool GetBoolArg(const std::string& strArg, bool fDefault)
{
    return settings.GetBoolArg(strArg, fDefault);
}

bool SoftSetArg(const std::string& strArg, const std::string& strValue)
{
    return settings.SoftSetArg(strArg, strValue);
}

bool SoftSetBoolArg(const std::string& strArg, bool fValue)
{
    return settings.SoftSetBoolArg(strArg, fValue);
}


static const int screenWidth = 79;
static const int optIndent = 2;
static const int msgIndent = 7;

std::string HelpMessageGroup(const std::string &message) {
    return std::string(message) + std::string("\n\n");
}

std::string HelpMessageOpt(const std::string &option, const std::string &message) {
    return std::string(optIndent,' ') + std::string(option) +
           std::string("\n") + std::string(msgIndent,' ') +
           FormatParagraph(message, screenWidth - msgIndent, msgIndent) +
           std::string("\n\n");
}

boost::filesystem::path GetConfigFile()
{
    return settings.GetConfigFile();
}

boost::filesystem::path GetMasternodeConfigFile()
{
    boost::filesystem::path pathConfigFile(GetArg("-mnconf", "masternode.conf"));
    if (!pathConfigFile.is_complete()) pathConfigFile = GetDataDir() / pathConfigFile;
    return pathConfigFile;
}

void ReadConfigFile(map<string, string>& mapSettingsRet,
    map<string, vector<string> >& mapMultiSettingsRet)
{
    boost::filesystem::ifstream streamConfig(GetConfigFile());
    if (!streamConfig.good()) {
        // Create empty divi.conf if it does not exist
        FILE* configFile = fopen(GetConfigFile().string().c_str(), "a");
        if (configFile != NULL)
            fclose(configFile);
        return; // Nothing to read, so just return
    }

    set<string> setOptions;
    setOptions.insert("*");

    for (boost::program_options::detail::config_file_iterator it(streamConfig, setOptions), end; it != end; ++it) {
        // Don't overwrite existing settings so command line settings override divi.conf
        string strKey = string("-") + it->string_key;
        string strValue = it->value[0];
        InterpretNegativeSetting(strKey, strValue);
        if (mapSettingsRet.count(strKey) == 0)
            mapSettingsRet[strKey] = strValue;
        mapMultiSettingsRet[strKey].push_back(strValue);
    }
    // If datadir is changed in .conf file:
    ClearDatadirCache();
}

#ifndef WIN32
boost::filesystem::path GetPidFile()
{
    boost::filesystem::path pathPidFile(GetArg("-pid", "divid.pid"));
    if (!pathPidFile.is_complete()) pathPidFile = GetDataDir() / pathPidFile;
    return pathPidFile;
}

void CreatePidFile(const boost::filesystem::path& path, pid_t pid)
{
    FILE* file = fopen(path.string().c_str(), "w");
    if (file) {
        fprintf(file, "%d\n", pid);
        fclose(file);
    }
}
#endif

bool RenameOver(boost::filesystem::path src, boost::filesystem::path dest)
{
#ifdef WIN32
    return MoveFileExA(src.string().c_str(), dest.string().c_str(),
               MOVEFILE_REPLACE_EXISTING) != 0;
#else
    int rc = std::rename(src.string().c_str(), dest.string().c_str());
    return (rc == 0);
#endif /* WIN32 */
}

void FileCommit(FILE* fileout)
{
    fflush(fileout); // harmless if redundantly called
#ifdef WIN32
    HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(fileout));
    FlushFileBuffers(hFile);
#else
#if defined(__linux__) || defined(__NetBSD__)
    fdatasync(fileno(fileout));
#elif defined(__APPLE__) && defined(F_FULLFSYNC)
    fcntl(fileno(fileout), F_FULLFSYNC, 0);
#else
    fsync(fileno(fileout));
#endif
#endif
}

bool TruncateFile(FILE* file, unsigned int length)
{
#if defined(WIN32)
    return _chsize(_fileno(file), length) == 0;
#else
    return ftruncate(fileno(file), length) == 0;
#endif
}

/**
 * this function tries to raise the file descriptor limit to the requested number.
 * It returns the actual file descriptor limit (which may be more or less than nMinFD)
 */
int RaiseFileDescriptorLimit(int nMinFD)
{
#if defined(WIN32)
    return 2048;
#else
    struct rlimit limitFD;
    if (getrlimit(RLIMIT_NOFILE, &limitFD) != -1) {
        if (limitFD.rlim_cur < (rlim_t)nMinFD) {
            limitFD.rlim_cur = nMinFD;
            if (limitFD.rlim_cur > limitFD.rlim_max)
                limitFD.rlim_cur = limitFD.rlim_max;
            setrlimit(RLIMIT_NOFILE, &limitFD);
            getrlimit(RLIMIT_NOFILE, &limitFD);
        }
        return limitFD.rlim_cur;
    }
    return nMinFD; // getrlimit failed, assume it's fine
#endif
}

/**
 * this function tries to make a particular range of a file allocated (corresponding to disk space)
 * it is advisory, and the range specified in the arguments will never contain live data
 */
void AllocateFileRange(FILE* file, unsigned int offset, unsigned int length)
{
#if defined(WIN32)
    // Windows-specific version
    HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(file));
    LARGE_INTEGER nFileSize;
    int64_t nEndPos = (int64_t)offset + length;
    nFileSize.u.LowPart = nEndPos & 0xFFFFFFFF;
    nFileSize.u.HighPart = nEndPos >> 32;
    SetFilePointerEx(hFile, nFileSize, 0, FILE_BEGIN);
    SetEndOfFile(hFile);
#elif defined(MAC_OSX)
    // OSX specific version
    fstore_t fst;
    fst.fst_flags = F_ALLOCATECONTIG;
    fst.fst_posmode = F_PEOFPOSMODE;
    fst.fst_offset = 0;
    fst.fst_length = (off_t)offset + length;
    fst.fst_bytesalloc = 0;
    if (fcntl(fileno(file), F_PREALLOCATE, &fst) == -1) {
        fst.fst_flags = F_ALLOCATEALL;
        fcntl(fileno(file), F_PREALLOCATE, &fst);
    }
    ftruncate(fileno(file), fst.fst_length);
#elif defined(__linux__)
    // Version using posix_fallocate
    off_t nEndPos = (off_t)offset + length;
    posix_fallocate(fileno(file), 0, nEndPos);
#else
    // Fallback version
    // TODO: just write one byte per block
    static const char buf[65536] = {};
    fseek(file, offset, SEEK_SET);
    while (length > 0) {
        unsigned int now = 65536;
        if (length < now)
            now = length;
        fwrite(buf, 1, now, file); // allowed to fail; this function is advisory anyway
        length -= now;
    }
#endif
}

void ShrinkDebugFile()
{
    // Scroll debug.log if it's getting too big
    boost::filesystem::path pathLog = GetDataDir() / "debug.log";
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

boost::filesystem::path GetTempPath()
{
#if BOOST_FILESYSTEM_VERSION == 3
    return boost::filesystem::temp_directory_path();
#else
    // TODO: remove when we don't support filesystem v2 anymore
    boost::filesystem::path path;
#ifdef WIN32
    char pszPath[MAX_PATH] = "";

    if (GetTempPathA(MAX_PATH, pszPath))
        path = boost::filesystem::path(pszPath);
#else
    path = boost::filesystem::path("/tmp");
#endif
    if (path.empty() || !boost::filesystem::is_directory(path)) {
        LogPrintf("GetTempPath(): failed to find temp path\n");
        return boost::filesystem::path("");
    }
    return path;
#endif
}

void runCommand(std::string strCommand)
{
    int nErr = ::system(strCommand.c_str());
    if (nErr)
        LogPrintf("runCommand error: system(%s) returned %d\n", strCommand, nErr);
}

void RenameThread(const char* name)
{
#if defined(PR_SET_NAME)
    // Only the first 15 characters are used (16 - NUL terminator)
    ::prctl(PR_SET_NAME, name, 0, 0, 0);
#elif 0 && (defined(__FreeBSD__) || defined(__OpenBSD__))
    // TODO: This is currently disabled because it needs to be verified to work
    //       on FreeBSD or OpenBSD first. When verified the '0 &&' part can be
    //       removed.
    pthread_set_name_np(pthread_self(), name);

#elif defined(MAC_OSX) && defined(__MAC_OS_X_VERSION_MAX_ALLOWED)

// pthread_setname_np is XCode 10.6-and-later
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 1060
    pthread_setname_np(name);
#endif

#else
    // Prevent warnings for unused parameters...
    (void)name;
#endif
}

void SetupEnvironment()
{
// On most POSIX systems (e.g. Linux, but not BSD) the environment's locale
// may be invalid, in which case the "C" locale is used as fallback.
#if !defined(WIN32) && !defined(MAC_OSX) && !defined(__FreeBSD__) && !defined(__OpenBSD__)
    try {
        std::locale(""); // Raises a runtime error if current locale is invalid
    } catch (const std::runtime_error&) {
        setenv("LC_ALL", "C", 1);
    }
#endif
    // The path locale is lazy initialized and to avoid deinitialization errors
    // in multithreading environments, it is set explicitly by the main thread.
    // A dummy locale is used to extract the internal default locale, used by
    // boost::filesystem::path, which is then used to explicitly imbue the path.
    std::locale loc = boost::filesystem::path::imbue(std::locale::classic());
    boost::filesystem::path::imbue(loc);
}

void SetThreadPriority(int nPriority)
{
#ifdef WIN32
    SetThreadPriority(GetCurrentThread(), nPriority);
#else // WIN32
#ifdef PRIO_THREAD
    setpriority(PRIO_THREAD, 0, nPriority);
#else  // PRIO_THREAD
    setpriority(PRIO_PROCESS, 0, nPriority);
#endif // PRIO_THREAD
#endif // WIN32
}
