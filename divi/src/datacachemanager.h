#ifndef DATA_CACHE_MANAGER_H
#define DATA_CACHE_MANAGER_H
#include <boost/filesystem/path.hpp>
#include <uiMessenger.h>
class CMasternodeMan;
class CMasternodePayments;
class CNetFulfilledRequestManager;
class CClientUIInterface;

class DataCacheManager
{
private:
    CNetFulfilledRequestManager& networkRequestManager_;
    boost::filesystem::path pathDB;
    UIMessenger uiMessenger_;
    bool litemode_;
public:
    DataCacheManager(
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