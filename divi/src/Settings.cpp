#include <Settings.h>
#include <string>
#include <utilstrencodings.h>
#include <DataDirectory.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/program_options/detail/config_file.hpp>
#include <set>

std::string CopyableSettings::GetArg(const std::string& strArg, const std::string& strDefault) const
{
    if (mapArgs_.count(strArg))
        return mapArgs_[strArg];
    return strDefault;
}

int64_t CopyableSettings::GetArg(const std::string& strArg, int64_t nDefault) const
{
    if (mapArgs_.count(strArg))
        return atoi64(mapArgs_[strArg]);
    return nDefault;
}

static bool InterpretBool(const std::string& strValue)
{
    if (strValue.empty())
        return true;
    return (atoi(strValue) != 0);
}

static void InterpretNegativeSetting(std::string& strKey, std::string& strValue)
{
    if (strKey.length()>3 && strKey[0]=='-' && strKey[1]=='n' && strKey[2]=='o') {
        strKey = "-" + strKey.substr(3);
        strValue = InterpretBool(strValue) ? "0" : "1";
    }
}

bool CopyableSettings::GetBoolArg(const std::string& strArg, bool fDefault) const
{
    if (mapArgs_.count(strArg))
        return InterpretBool(mapArgs_[strArg]);
    return fDefault;
}

bool CopyableSettings::SoftSetArg(const std::string& strArg, const std::string& strValue)
{
    if (mapArgs_.count(strArg))
        return false;
    mapArgs_[strArg] = strValue;
    return true;
}

bool CopyableSettings::SoftSetBoolArg(const std::string& strArg, bool fValue)
{
    if (fValue)
        return SoftSetArg(strArg, std::string("1"));
    else
        return SoftSetArg(strArg, std::string("0"));
}

void CopyableSettings::ForceRemoveArg(const std::string &strArg)
{
    mapArgs_.erase(strArg);
}

bool CopyableSettings::ParameterIsSet (const std::string& key) const
{
    return mapArgs_.count(key) > 0;
}

std::string CopyableSettings::GetParameter(const std::string& key) const
{
    if(ParameterIsSet(key))
    {
        return mapArgs_[key];
    }
    else
    {
        return "";
    }
}

void CopyableSettings::SetParameter (const std::string& key, const std::string& value)
{
    mapArgs_[key] = value;
}

void CopyableSettings::ClearParameter ()
{
    mapArgs_.clear();
}

bool CopyableSettings::ParameterIsSetForMultiArgs (const std::string& key) const
{
    return mapMultiArgs_.count(key) > 0;
}

void CopyableSettings::ParseParameters(int argc, const char* const argv[])
{
    ClearParameter();
    mapMultiArgs_.clear();

    for (int i = 1; i < argc; i++) {
        std::string str(argv[i]);
        std::string strValue;
        size_t is_index = str.find('=');
        if (is_index != std::string::npos) {
            strValue = str.substr(is_index + 1);
            str = str.substr(0, is_index);
        }
#ifdef WIN32
        boost::to_lower(str);
        if (boost::algorithm::starts_with(str, "/"))
            str = "-" + str.substr(1);
#endif

        if (str[0] != '-')
            break;

        // Interpret --foo as -foo.
        // If both --foo and -foo are set, the last takes effect.
        if (str.length() > 1 && str[1] == '-')
            str = str.substr(1);
        InterpretNegativeSetting(str, strValue);

        SetParameter(str, strValue);
        mapMultiArgs_[str].push_back(strValue);
    }
}

boost::filesystem::path CopyableSettings::GetConfigFile()
{
    boost::filesystem::path pathConfigFile(GetArg("-conf", "divi.conf"));
    if (!pathConfigFile.is_complete())
        pathConfigFile = GetDataDir(false) / pathConfigFile;

    return pathConfigFile;
}

void CopyableSettings::ReadConfigFile()
{
    boost::filesystem::ifstream streamConfig(GetConfigFile());
    if (!streamConfig.good()) {
        // Create empty divi.conf if it does not exist
        FILE* configFile = fopen(GetConfigFile().string().c_str(), "a");
        if (configFile != NULL)
            fclose(configFile);
        return; // Nothing to read, so just return
    }

    std::set<std::string> setOptions;
    setOptions.insert("*");

    for (boost::program_options::detail::config_file_iterator it(streamConfig, setOptions), end; it != end; ++it) {
        // Don't overwrite existing settings so command line settings override divi.conf
        std::string strKey = std::string("-") + it->string_key;
        std::string strValue = it->value[0];
        InterpretNegativeSetting(strKey, strValue);
        if (mapArgs_.count(strKey) == 0)
            mapArgs_[strKey] = strValue;
        mapMultiArgs_[strKey].push_back(strValue);
    }
    // If datadir is changed in .conf file:
    ClearDatadirCache();
}

unsigned CopyableSettings::MaxNumberOfPoSCombinableInputs() const
{
    constexpr unsigned int MAX_KERNEL_COMBINED_INPUTS = 20;
    return MAX_KERNEL_COMBINED_INPUTS;
}
int CopyableSettings::MaxFutureBlockDrift() const
{
    constexpr int maximumFutureBlockDrift = 180;
    return maximumFutureBlockDrift;
}