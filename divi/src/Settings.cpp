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
        return mapArgs_.find(strArg)->second;
    return strDefault;
}

int64_t CopyableSettings::GetArg(const std::string& strArg, int64_t nDefault) const
{
    if (mapArgs_.count(strArg))
        return atoi64(mapArgs_.find(strArg)->second);
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
    // -no-somesetting=somevalue // parses as -somesetting=0
    if (strKey.length()>3 && strKey[0]=='-' && strKey[1]=='n' && strKey[2]=='o') {
        strKey = "-" + strKey.substr(3);
        strValue = InterpretBool(strValue) ? "0" : "1";
    }
}

bool CopyableSettings::GetBoolArg(const std::string& strArg, bool fDefault) const
{
    if (mapArgs_.count(strArg))
        return InterpretBool(mapArgs_.find(strArg)->second);
    return fDefault;
}

bool CopyableSettings::SoftSetArg(const std::string& strArg, const std::string& strValue)
{
    if (ParameterIsSet(strArg))
        return false;
    SetParameter(strArg,strValue,true);
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
        return mapArgs_.find(key)->second;
    }
    else
    {
        return "";
    }
}

const std::vector<std::string>& CopyableSettings::GetMultiParameter(const std::string& key) const
{
    static std::vector<std::string> empty;
    if(ParameterIsSetForMultiArgs(key))
    {
        return mapMultiArgs_.find(key)->second;
    }
    else
    {
        return empty;
    }
}

void CopyableSettings::SetParameter (const std::string& key, const std::string& value, const bool setOnceOnly)
{
    // Interpret --foo as -foo.
    // If both --foo and -foo are set, the last takes effect.
    // ---foo is ignored
    std::string prunedKey = key;
    std::string parsedValue = value;
    if (prunedKey.length() > 1 && prunedKey[0] == '-' && prunedKey[1] == '-')
        prunedKey = prunedKey.substr(1);
    if (prunedKey.length() > 1 && prunedKey[0] == '-' && prunedKey[1] == '-')
        return;

    InterpretNegativeSetting(prunedKey, parsedValue);
    if(!setOnceOnly || mapArgs_.count(prunedKey) == 0)
    {
        mapArgs_[prunedKey] = parsedValue;
    }
    mapMultiArgs_[prunedKey].push_back(parsedValue);
}

void CopyableSettings::ClearParameter ()
{
    mapArgs_.clear();
    mapMultiArgs_.clear();
}

bool CopyableSettings::ParameterIsSetForMultiArgs (const std::string& key) const
{
    return mapMultiArgs_.count(key) > 0;
}

void CopyableSettings::ParseParameters(int argc, const char* const argv[])
{
    ClearParameter();

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

        SetParameter(str, strValue,false);
    }
    reindexingBlocks_ = reindexingWasRequested();
}

boost::filesystem::path CopyableSettings::GetConfigFile() const
{
    boost::filesystem::path pathConfigFile(GetArg("-conf", "divi.conf"));
    if (!pathConfigFile.is_complete())
        pathConfigFile = GetDataDir(false) / pathConfigFile;

    return pathConfigFile;
}

static std::string ParseNetworkSpecificFlag(const std::string& key, const CopyableSettings& settings)
{
    size_t separatorPosition = key.find('.');
    if(separatorPosition == std::string::npos)
    {
        return key;
    }
    const std::string networkType = key.substr(0,separatorPosition);
    const std::string trimmedKey = key.substr(separatorPosition+1, std::string::npos);
    if(networkType.empty()) return key;
    if(settings.ParameterIsSet("testnet") && networkType == "test")
    {
        return trimmedKey;
    }
    if(!settings.ParameterIsSet("testnet") && !settings.ParameterIsSet("regtest"))
    {
        return trimmedKey;
    }
    else
    {
        return std::string("");
    }
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
        const std::string key = ParseNetworkSpecificFlag(it->string_key,*this);
        if(key.empty()) continue;
        SetParameter(std::string("-") + key, it->value[0],true);
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

unsigned CopyableSettings::NumerOfParameters() const
{
    return mapArgs_.size();
}
unsigned CopyableSettings::NumerOfMultiParameters() const
{
    return mapMultiArgs_.size();
}

bool CopyableSettings::debugModeIsEnabled() const
{
    const std::vector<std::string>& categories = GetMultiParameter("-debug");
    const bool anyNegativeDebugCategory = std::find(categories.begin(), categories.end(), std::string("0")) != categories.end();
    bool debug = !(GetBoolArg("-nodebug", false) || anyNegativeDebugCategory) && !categories.empty();
    return debug;
}

void CopyableSettings::setFileImportingFlag(const bool updatedValue)
{
    importingFiles_ = updatedValue;
}
bool CopyableSettings::isImportingFiles() const
{
    return importingFiles_;
}

void CopyableSettings::setReindexingFlag(const bool updatedValue)
{
    reindexingBlocks_ = updatedValue;
}
bool CopyableSettings::isReindexingBlocks() const
{
    return reindexingBlocks_;
}
bool CopyableSettings::reindexingWasRequested() const
{
    return GetBoolArg("-reindex",false);
}

void CopyableSettings::setStartupBlockVerificationFlag(const bool updatedValue)
{
    startupBlockVerificationInProgress_ = updatedValue;
}
bool CopyableSettings::isStartupVerifyingBlocks() const
{
    return startupBlockVerificationInProgress_;
}