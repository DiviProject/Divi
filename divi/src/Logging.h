#ifndef LOGGING_H
#define LOGGING_H
#include <string>
#include "tinyformat.h"
#include <vector>
#include <map>

/* Variables used by logging*/
extern bool fPrintToConsole;
extern bool fPrintToDebugLog;
extern bool fDebug;
extern volatile bool fReopenDebugLog;
extern bool fLogTimestamps;
extern std::map<std::string, std::vector<std::string> > mapMultiArgs;

/** Return true if log accepts specified category */
bool LogAcceptCategory(const char* category);
/** Send a string to the log output */
int LogPrintStr(const std::string& str);

#define LogPrintf(...) LogPrint(NULL, __VA_ARGS__)

/**
 * When we switch to C++11, this can be switched to variadic templates instead
 * of this macro-based construction (see tinyformat.h).
 */
#define MAKE_ERROR_AND_LOG_FUNC(n)                                                              \
    /**   Print to debug.log if -debug=category switch is given OR category is NULL. */         \
    template <TINYFORMAT_ARGTYPES(n)>                                                           \
    static inline int LogPrint(const char* category, const char* format, TINYFORMAT_VARARGS(n)) \
    {                                                                                           \
        if (!LogAcceptCategory(category)) return 0;                                             \
        const std::string base = tfm::format(format, TINYFORMAT_PASSARGS(n));                   \
        if (category == nullptr)                                                                \
            return LogPrintStr(base);                                                           \
        return LogPrintStr(tfm::format("[%s] %s", category, base.c_str()));                     \
    }                                                                                           \
    /**   Log error and return false */                                                         \
    template <TINYFORMAT_ARGTYPES(n)>                                                           \
    static inline bool error(const char* format, TINYFORMAT_VARARGS(n))                         \
    {                                                                                           \
        LogPrintStr(std::string("ERROR: ") + tfm::format(format, TINYFORMAT_PASSARGS(n)) + "\n");            \
        return false;                                                                           \
    }

TINYFORMAT_FOREACH_ARGNUM(MAKE_ERROR_AND_LOG_FUNC)

/**
 * Zero-arg versions of logging and error, these are not covered by
 * TINYFORMAT_FOREACH_ARGNUM
 */
static inline int LogPrint(const char* category, const char* format)
{
    if (!LogAcceptCategory(category)) return 0;
    return LogPrintStr(format);
}
static inline bool error(const char* format)
{
    LogPrintStr(std::string("ERROR: ") + format + "\n");
    return false;
}

#endif //LOGGING_H
