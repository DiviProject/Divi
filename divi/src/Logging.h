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

/** Explicitly converts a particular value to a string for logging.  This is
 *  used for some types (e.g. uint256) that are not literally passed on to
 *  the format engine.  Template specialisations are defined in source files
 *  that are linked into the respective libraries.  */
template <typename T>
  std::string LogFormatConvert(const T& v);

/** Helper type for converting some types with LogFormatConvert, while keeping
 *  others literal at whatever they are.  */
template <typename T>
  struct LogFormatConvertWorker
{
    static const T& apply(const T& v)
    {
        return v;
    }
};
#define LOG_WITH_CONVERSION(type) \
    class type; \
    template <> \
      std::string LogFormatConvert<type>(const type& v); \
    template <> \
      struct LogFormatConvertWorker<const type&> \
    { \
        static std::string apply(const type& v) \
        { \
            return LogFormatConvert(v); \
        } \
    };
LOG_WITH_CONVERSION(CLockLocation)
LOG_WITH_CONVERSION(uint256)

/* Defined in Logging-common.cpp.  */
LOG_WITH_CONVERSION(CAddress)
LOG_WITH_CONVERSION(CBitcoinAddress)
LOG_WITH_CONVERSION(CBlock)
LOG_WITH_CONVERSION(CNetAddr)
LOG_WITH_CONVERSION(CFeeRate)
LOG_WITH_CONVERSION(CInv)
LOG_WITH_CONVERSION(CKeyID)
LOG_WITH_CONVERSION(CScript)
LOG_WITH_CONVERSION(CService)
LOG_WITH_CONVERSION(CTxIn)
LOG_WITH_CONVERSION(CTxOut)
LOG_WITH_CONVERSION(CTransaction)

/* Defined in Logging-server.cpp.  */
LOG_WITH_CONVERSION(CBlockFileInfo)
LOG_WITH_CONVERSION(CBlockIndex)
LOG_WITH_CONVERSION(CBlockRewards)

/* Defined in Logging-wallet.cpp.  */
LOG_WITH_CONVERSION(CWalletTx)

/** This macro defines a custom implementation of LogFormatConvert for
 *  a type that then just calls through to the instance's ToString
 *  method.  The actual definition has to be done in a source file
 *  linked in with the correct library, since it needs the definition of
 *  the type in question.  */
#define LOG_FORMAT_WITH_TOSTRING(type) \
    template <> \
      std::string LogFormatConvert<type>(const type& v) \
    { \
        return v.ToString(); \
    }

/** Formats a string (consisting of a format template and zero or more
 *  arguments) for logging.  This mostly invokes tinyformat, but has some
 *  extra tweaks like supporting zero arguments and applying conversion
 *  for some types.  */
template <typename... Args>
  std::string LogFormat(const char* format, const Args&... args)
{
    return tfm::format(format, LogFormatConvertWorker<decltype(args)>::apply(args)...);
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
