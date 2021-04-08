#ifndef LOGGING_H
#define LOGGING_H

#include "tinyformat.h"

#include <map>
#include <string>
#include <vector>

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

/** Formats a string (consisting of a format template and zero or more
 *  arguments) for logging.  This mostly invokes tinyformat, but has some
 *  extra tweaks like supporting zero arguments.  */
template <typename... Args>
  std::string LogFormat(const char* format, const Args&... args)
{
    return tfm::format(format, args...);
}
/* This is an explicit specialisation, so we have to declare it "inline"
   to prevent ODR violations.  */
template <>
  inline std::string LogFormat(const char* format)
{
    return format;
}

/** Logs a message if the -debug=category switch is given OR the category
 *  is NULL (general logs).  */
template <typename... Args>
  int LogPrint(const char* category, const char* format, const Args&... args)
{
    if (!LogAcceptCategory(category))
        return 0;

    const std::string base = LogFormat(format, args...);

    if (category == nullptr)
        return LogPrintStr(base);

    return LogPrintStr(tfm::format("[%s] %s", category, base));
}

/** Prints a log message without category.  */
template <typename... Args>
  int LogPrintf(const char* format, const Args&... args)
{
    return LogPrint(nullptr, format, args...);
}

/** Logs an error and returns false.  */
template <typename... Args>
  bool error(const char* format, const Args&... args)
{
    LogPrintStr(tfm::format("ERROR: %s", LogFormat(format, args...)));
    return false;
}

#endif //LOGGING_H
