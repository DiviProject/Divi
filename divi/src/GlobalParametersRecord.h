#ifndef GLOBAL_PARAMETERS_RECORD_H
#define GLOBAL_PARAMETERS_RECORD_H
#include <string>
#include <map>

class GlobalParametersRecord
{

private:
    std::map<std::string, std::string>& mapArgs_;
public:
    GlobalParametersRecord(
        std::map<std::string, std::string>& mapArgs
        ): mapArgs_(mapArgs)
    {
    }

    ~GlobalParametersRecord() 
    {
    }
    
    std::string GetArg(const std::string& strArg, const std::string& strDefault);

    int64_t GetArg(const std::string& strArg, int64_t nDefault);

    bool GetBoolArg(const std::string& strArg, bool fDefault);

    bool SoftSetArg(const std::string& strArg, const std::string& strValue);

    bool SoftSetBoolArg(const std::string& strArg, bool fValue);
};

#endif //GLOBAL_PARAMETERS_RECORD_H