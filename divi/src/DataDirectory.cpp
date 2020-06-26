#include <DataDirectory.h>

#if defined(HAVE_CONFIG_H)
#include "config/divi-config.h"
#endif

#include <boost/filesystem.hpp>
#include <sync.h>

#ifdef WIN32
#include <shlobj.h>
#endif

#include <chainparamsbase.h>
#include <Logging.h>

#include <Settings.h>
#include <vector>

#ifdef WIN32
boost::filesystem::path GetSpecialFolderPath(int nFolder, bool fCreate)
{
    namespace fs = boost::filesystem;

    char pszPath[MAX_PATH] = "";

    if (SHGetSpecialFolderPathA(NULL, pszPath, nFolder, fCreate)) {
        return fs::path(pszPath);
    }

    LogPrintf("SHGetSpecialFolderPathA() failed, could not obtain requested path.\n");
    return fs::path("");
}
#endif
extern std::map<std::string, std::vector<std::string> > mapMultiArgs;
Settings& settingsDataDirectory = Settings::instance(mapArgs, mapMultiArgs);

/**
 * Ignores exceptions thrown by Boost's create_directory if the requested directory exists.
 * Specifically handles case where path p exists, but it wasn't possible for the user to
 * write to the parent directory.
 */
bool TryCreateDirectory(const boost::filesystem::path& p)
{
    try {
        return boost::filesystem::create_directory(p);
    } catch (boost::filesystem::filesystem_error) {
        if (!boost::filesystem::exists(p) || !boost::filesystem::is_directory(p))
            throw;
    }

    // create_directory didn't create the directory, it had to have existed already
    return false;
}

boost::filesystem::path GetDefaultDataDir()
{
    namespace fs = boost::filesystem;
// Windows < Vista: C:\Documents and Settings\Username\Application Data\DIVI
// Windows >= Vista: C:\Users\Username\AppData\Roaming\DIVI
// Mac: ~/Library/Application Support/DIVI
// Unix: ~/.divi
#ifdef WIN32
    // Windows
    return GetSpecialFolderPath(CSIDL_APPDATA) / "DIVI";
#else
    fs::path pathRet;
    char* pszHome = getenv("HOME");
    if (pszHome == NULL || strlen(pszHome) == 0)
        pathRet = fs::path("/");
    else
        pathRet = fs::path(pszHome);
#ifdef MAC_OSX
    // Mac
    pathRet /= "Library/Application Support";
    TryCreateDirectory(pathRet);
    return pathRet / "DIVI";
#else
    // Unix
    return pathRet / ".divi";
#endif
#endif
}

static boost::filesystem::path pathCached;
static boost::filesystem::path pathCachedNetSpecific;
static CCriticalSection csPathCached;

const boost::filesystem::path& GetDataDir(bool fNetSpecific)
{
    namespace fs = boost::filesystem;

    LOCK(csPathCached);

    fs::path& path = fNetSpecific ? pathCachedNetSpecific : pathCached;

    // This can be called during exceptions by LogPrintf(), so we cache the
    // value so we don't have to do memory allocations after that.
    if (!path.empty())
        return path;

    if (settingsDataDirectory.ParameterIsSet("-datadir")) {
        path = fs::system_complete(settingsDataDirectory.GetArg("-datadir", ""));
        if (!fs::is_directory(path)) {
            path = "";
            return path;
        }
    } else {
        path = GetDefaultDataDir();
    }
    if (fNetSpecific) {
        path /= BaseParams().DataDir();
    }

    fs::create_directories(path);

    return path;
}

void ClearDatadirCache()
{
    pathCached = boost::filesystem::path();
    pathCachedNetSpecific = boost::filesystem::path();
}