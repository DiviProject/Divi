#include "datacachemanager.h"
#include "flat-database.h"
#include "ui_interface.h"
#include "masternodeman.h"
#include "masternode-payments.h"
#include "netfulfilledman.h"

std::string UIMessenger::translate(const char* psz)
{
    boost::optional<std::string> rv = uiInterface_.Translate(psz);
    return rv ? (*rv) : psz;
}
std::string UIMessenger::translate(std::string psz)
{
    return translate(psz.c_str());
}
std::string UIMessenger::translate(const std::string& translatable, const std::string& untranslatable)
{
    return translate(translatable) + untranslatable;
}

bool UIMessenger::InitError(const std::string& str, std::string untranslateableString)
{
    uiInterface_.ThreadSafeMessageBox(translate(str,untranslateableString), "", CClientUIInterface::MSG_ERROR);
    return false;
}
bool UIMessenger::InitWarning(const std::string& str, std::string untranslateableString)
{
    uiInterface_.ThreadSafeMessageBox(translate(str,untranslateableString), "", CClientUIInterface::MSG_WARNING);
    return true;
}
bool UIMessenger::InitMessage(const std::string& str, std::string untranslateableString)
{
    uiInterface_.InitMessage(translate(str,untranslateableString));
    return true;
}



void DataCacheManager::StoreDataCaches()
{
    if (!litemode_) {
        CFlatDB<CMasternodeMan> flatdb1("mncache.dat", "magicMasternodeCache");
        flatdb1.Dump(masternodeManager_);
        CFlatDB<CMasternodePayments> flatdb2("mnpayments.dat", "magicMasternodePaymentsCache");
        flatdb2.Dump(masternodePayments_);
        CFlatDB<CNetFulfilledRequestManager> flatdb4("netfulfilled.dat", "magicFulfilledCache");
        flatdb4.Dump(networkRequestManager_);
    }
}

bool DataCacheManager::LoadDataCaches()
{
    if (!litemode_) {
        std::string strDBName;

        strDBName = "mncache.dat";
        uiMessenger_.InitMessage("Loading masternode cache...");
        CFlatDB<CMasternodeMan> flatdb1(strDBName, "magicMasternodeCache");
        if(!flatdb1.Load(masternodeManager_)) {
            return uiMessenger_.InitError("Failed to load masternode cache from", "\n" + (pathDB / strDBName).string());
        }

        if(masternodeManager_.size()) {
            strDBName = "mnpayments.dat";
            uiMessenger_.InitMessage("Loading masternode payment cache...");
            CFlatDB<CMasternodePayments> flatdb2(strDBName, "magicMasternodePaymentsCache");
            if(!flatdb2.Load(masternodePayments_)) {
                return uiMessenger_.InitError("Failed to load masternode payments cache from", "\n" + (pathDB / strDBName).string());
            }
        } else {
            uiMessenger_.InitMessage("Masternode cache is empty, skipping payments and governance cache...");
        }

        strDBName = "netfulfilled.dat";
        uiMessenger_.InitMessage("Loading fulfilled requests cache...");
        CFlatDB<CNetFulfilledRequestManager> flatdb4(strDBName, "magicFulfilledCache");
        if(!flatdb4.Load(networkRequestManager_)) {
            return uiMessenger_.InitError("Failed to load fulfilled requests cache from", "\n" + (pathDB / strDBName).string());
        }
    }

    return true;
}