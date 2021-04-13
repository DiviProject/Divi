#ifndef SETTINGS_H
#define SETTINGS_H
#include <string>
#include <map>
#include <vector>
#include <boost/filesystem.hpp>

class CopyableSettings
{
protected:
    std::map<std::string, std::string> mapArgs_;
    std::map<std::string, std::vector<std::string> > mapMultiArgs_;

public:
    CopyableSettings(
        ): mapArgs_()
        , mapMultiArgs_()
    {
    }

    std::string GetArg(const std::string& strArg, const std::string& strDefault) const;

    int64_t GetArg(const std::string& strArg, int64_t nDefault) const;

    bool GetBoolArg(const std::string& strArg, bool fDefault) const;

    bool SoftSetArg(const std::string& strArg, const std::string& strValue);

    bool SoftSetBoolArg(const std::string& strArg, bool fValue);

    void ForceRemoveArg(const std::string &strArg);

    bool ParameterIsSet (const std::string& key) const;

    std::string GetParameter(const std::string& key) const;
    const std::vector<std::string>& GetMultiParameter(const std::string& key) const;

    void SetParameter(const std::string& key, const std::string& value);

    void ClearParameter();

    bool ParameterIsSetForMultiArgs (const std::string& key) const;

    void ParseParameters(int argc, const char* const argv[]);

    boost::filesystem::path GetConfigFile() const;

    unsigned NumerOfParameters() const;
    unsigned NumerOfMultiParameters() const;
    void ReadConfigFile();
    unsigned MaxNumberOfPoSCombinableInputs() const;
    int MaxFutureBlockDrift() const;
};

class Settings: public CopyableSettings
{
private:

    Settings(): CopyableSettings()
    {
    }

public:

    static Settings& instance()
    {
        static Settings settings;
        return settings;
    }

};

#endif //SETTINGS_H
