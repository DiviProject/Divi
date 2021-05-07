#include <PeerBanningService.h>

#include <Settings.h>
// Static stuff
std::map<CNetAddr, int64_t> PeerBanningService::setBanned;
CCriticalSection PeerBanningService::cs_setBanned;

void PeerBanningService::ClearBanned()
{
    LOCK(cs_setBanned);
    setBanned.clear();
}

std::string PeerBanningService::ListBanned()
{
    std::string bannedIps = "[\n";
    LOCK(cs_setBanned);
    for(auto banned: setBanned)
    {
        bannedIps += banned.first.ToString();
        bannedIps += ",\n";
    }
    bannedIps += "]";
    return bannedIps;
}

bool PeerBanningService::IsBanned(int64_t currentTime, CNetAddr ip)
{
    bool fResult = false;
    {
        LOCK(cs_setBanned);
        std::map<CNetAddr, int64_t>::iterator i = setBanned.find(ip);
        if (i != setBanned.end()) {
            int64_t t = (*i).second;
            if (currentTime < t)
                fResult = true;
        }
    }
    return fResult;
}

bool PeerBanningService::Ban(int64_t currentTime, const CNetAddr& addr)
{
    static const Settings& settings = Settings::instance();
    int64_t banTime = currentTime + settings.GetArg("-bantime", 60 * 60 * 24); // Default 24-hour ban
    return Ban(addr,banTime);
}

bool PeerBanningService::Ban(const CNetAddr& addr, int64_t banTime)
{
    {
        LOCK(cs_setBanned);
        if (setBanned[addr] < banTime)
            setBanned[addr] = banTime;
    }
    return true;
}