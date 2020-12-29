#ifndef DATA_CACHE_MANAGER_H
#define DATA_CACHE_MANAGER_H
#include <boost/filesystem/path.hpp>
class CMasternodeMan;
class CMasternodePayments;
class CNetFulfilledRequestManager;
class CClientUIInterface;

class UIMessenger
{
private:
    CClientUIInterface& uiInterface_;

    inline std::string translate(const char* psz);
    inline std::string translate(std::string psz);
    std::string translate(const std::string& translatable, const std::string& untranslatable);
public:
    UIMessenger(CClientUIInterface& uiInterface_in): uiInterface_(uiInterface_in){}

    bool InitError(const std::string& str, std::string untranslateableString = std::string());
    bool InitWarning(const std::string& str, std::string untranslateableString = std::string());
    bool InitMessage(const std::string& str, std::string untranslateableString = std::string());
};

class DataCacheManager
{
private:
    CMasternodeMan& masternodeManager_;
    CMasternodePayments& masternodePayments_;
    CNetFulfilledRequestManager& networkRequestManager_;
    boost::filesystem::path pathDB;
    UIMessenger uiMessenger_;
    bool litemode_;
public:
    DataCacheManager(
        CMasternodeMan& mnmanager,
        CMasternodePayments& mnPayments,
        CNetFulfilledRequestManager& networkRequestManager,
        const boost::filesystem::path& dataDirectory,
        CClientUIInterface& uiInterface_in,
        bool litemodeEnabled = false);
    DataCacheManager(
        const boost::filesystem::path& dataDirectory,
        CClientUIInterface& uiInterface_in,
        bool litemodeEnabled = false);

    void StoreDataCaches();
    bool LoadDataCaches();
};

#endif // DATA_CACHE_MANAGER_H