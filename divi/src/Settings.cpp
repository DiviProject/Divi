#include <Settings.h>
#include <string>
#include <utilstrencodings.h>

std::string Settings::GetArg(const std::string& strArg, const std::string& strDefault)
{
    if (mapArgs_.count(strArg))
        return mapArgs_[strArg];
    return strDefault;
}

int64_t Settings::GetArg(const std::string& strArg, int64_t nDefault)
{
    if (mapArgs_.count(strArg))
        return atoi64(mapArgs_[strArg]);
    return nDefault;
}

bool InterpretBool(const std::string& strValue)
{
    if (strValue.empty())
        return true;
    return (atoi(strValue) != 0);
}

bool Settings::GetBoolArg(const std::string& strArg, bool fDefault)
{
    if (mapArgs_.count(strArg))
        return InterpretBool(mapArgs_[strArg]);
    return fDefault;
}

bool Settings::SoftSetArg(const std::string& strArg, const std::string& strValue)
{
    if (mapArgs_.count(strArg))
        return false;
    mapArgs_[strArg] = strValue;
    return true;
}

bool Settings::SoftSetBoolArg(const std::string& strArg, bool fValue)
{
    if (fValue)
        return SoftSetArg(strArg, std::string("1"));
    else
        return SoftSetArg(strArg, std::string("0"));
}

void Settings::ForceRemoveArg(const std::string &strArg)
{
    mapArgs_.erase(strArg);
}