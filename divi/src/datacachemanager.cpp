#include "datacachemanager.h"
#include "flat-database.h"
#include "ui_interface.h"
#include "netfulfilledman.h"

DataCacheManager::DataCacheManager(
    CNetFulfilledRequestManager& networkRequestManager,
    const boost::filesystem::path& dataDirectory,
    CClientUIInterface& uiInterface_in,
    bool litemodeEnabled
    ): networkRequestManager_(networkRequestManager)
    , pathDB(dataDirectory)
    , uiMessenger_(uiInterface_in)
    , litemode_(litemodeEnabled)
{
}

DataCacheManager::DataCacheManager(
    const boost::filesystem::path& dataDirectory,
    CClientUIInterface& uiInterface_in,
    bool litemodeEnabled
    ): networkRequestManager_(netfulfilledman)
    , pathDB(dataDirectory)
    , uiMessenger_(uiInterface_in)
    , litemode_(litemodeEnabled)
{
}

void DataCacheManager::StoreDataCaches()
{
    if (!litemode_) {
        CFlatDB<CNetFulfilledRequestManager> flatdb4("netfulfilled.dat", "magicFulfilledCache");
        flatdb4.Dump(networkRequestManager_);
    }
}

bool DataCacheManager::LoadDataCaches()
{
    if (!litemode_) {
        std::string strDBName;

        strDBName = "netfulfilled.dat";
        uiMessenger_.InitMessage("Loading fulfilled requests cache...");
        CFlatDB<CNetFulfilledRequestManager> flatdb4(strDBName, "magicFulfilledCache");
        if(!flatdb4.Load(networkRequestManager_)) {
            return uiMessenger_.InitError("Failed to load fulfilled requests cache from", "\n" + (pathDB / strDBName).string());
        }
    }

    return true;
}