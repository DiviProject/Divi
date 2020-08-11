#ifndef SETTINGS_H
#define SETTINGS_H
#include <string>
#include <map>
#include <vector> 
#include <boost/filesystem.hpp>

class CopyableSettings 
{
protected:
    std::map<std::string, std::string>& mapArgs_;
    std::map<std::string, std::vector<std::string> >& mapMultiArgs_;

public:
    CopyableSettings(
        std::map<std::string, std::string>& mapArgs,
        std::map<std::string, std::vector<std::string> >& mapMultiArgs
        ): mapArgs_(mapArgs)
        , mapMultiArgs_(mapMultiArgs)
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

    void SetParameter (const std::string& key, const std::string& value);

    void ClearParameter (); 

    bool ParameterIsSetForMultiArgs (const std::string& key) const;

    void ParseParameters(int argc, const char* const argv[]);

    boost::filesystem::path GetConfigFile();

    void ReadConfigFile();
};

class Settings: public CopyableSettings
{
private:

    Settings(
        std::map<std::string, std::string>& mapArgs,
        std::map<std::string, std::vector<std::string> >& mapMultiArgs
        ): CopyableSettings(mapArgs, mapMultiArgs)
    {
    }

public:

    static Settings& instance(
        std::map<std::string, std::string>& mapArgs, 
        std::map<std::string, std::vector<std::string> >& mapMultiArgs) 
    {
        static Settings settings(mapArgs, mapMultiArgs);
        return settings;
    }
    
};

#endif //SETTINGS_H
