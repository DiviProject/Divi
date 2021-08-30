#include <PeerBanningService.h>

// Static stuff
constexpr int64_t lifetimeBan = int64_t( (~uint64_t(0)) >> 8 );
static_assert(lifetimeBan > 0,"Ban times should not be negative!");

std::map<CNetAddr, int64_t> PeerBanningService::setBanned;
CCriticalSection PeerBanningService::cs_setBanned;
int64_t PeerBanningService::defaultBanDuration = 0;

void PeerBanningService::SetDefaultBanDuration(int64_t banDuration)
{
    LOCK(cs_setBanned);
    defaultBanDuration = banDuration;
}

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
    int64_t banTime = currentTime + defaultBanDuration;
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

bool PeerBanningService::LifetimeBan(const CNetAddr& addr)
{
    return PeerBanningService::Ban(addr,lifetimeBan);
}