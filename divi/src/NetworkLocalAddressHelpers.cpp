#include <NetworkLocalAddressHelpers.h>

#include <map>

#include <Logging.h>
#include <random.h>
#include <sync.h>
#include <timedata.h>
#include <Settings.h>
#include <chainparams.h>

extern Settings& settings;
volatile bool fListen = true;
extern bool fDiscover;

CCriticalSection cs_mapLocalHost;
std::map<CNetAddr, LocalServiceInfo> mapLocalHost;
static bool vfReachable[NET_MAX] = {};
static bool vfLimited[NET_MAX] = {};
uint64_t nLocalServices = NODE_NETWORK;

bool IsListening()
{
    return fListen;
}
void setListeningFlag(bool updatedListenFlag)
{
    fListen = updatedListenFlag;
}

struct LocalServiceInfo {
    int nScore;
    int nPort;
};

LocalHostData::LocalHostData(
    const CNetAddr& addr,
    const LocalServiceInfo& info
    ): address(addr.ToString())
    , port(std::to_string(info.nPort))
    , score(std::to_string(info.nScore))
{
}
std::vector<LocalHostData> GetLocalHostData()
{
    std::vector<LocalHostData> localHostData;
    LOCK(cs_mapLocalHost);
    localHostData.reserve(mapLocalHost.size());
    for(const auto& addressAndInfoPair: mapLocalHost)
    {
        localHostData.emplace_back(addressAndInfoPair.first,addressAndInfoPair.second);
    }
    return localHostData;
}

int GetnScore(const CService& addr)
{
    LOCK(cs_mapLocalHost);
    if (mapLocalHost.count(addr) == LOCAL_NONE)
        return 0;
    return mapLocalHost[addr].nScore;
}

void SetReachable(enum Network net, bool fFlag)
{
    LOCK(cs_mapLocalHost);
    vfReachable[net] = fFlag;
    if (net == NET_IPV6 && fFlag)
        vfReachable[NET_IPV4] = true;
}

// learn a new local address
bool AddLocal(const CService& addr, int nScore)
{
    if (!addr.IsRoutable())
        return false;

    if (!fDiscover && nScore < LOCAL_MANUAL)
        return false;

    if (IsLimited(addr))
        return false;

    LogPrintf("AddLocal(%s,%i)\n", addr, nScore);

    {
        LOCK(cs_mapLocalHost);
        bool fAlready = mapLocalHost.count(addr) > 0;
        LocalServiceInfo& info = mapLocalHost[addr];
        if (!fAlready || nScore >= info.nScore) {
            info.nScore = nScore + (fAlready ? 1 : 0);
            info.nPort = addr.GetPort();
        }
        SetReachable(addr.GetNetwork());
    }

    return true;
}

bool AddLocal(const CNetAddr& addr, int nScore)
{
    return AddLocal(CService(addr, GetListenPort()), nScore);
}

bool RemoveLocal(const CService& addr)
{
    LOCK(cs_mapLocalHost);
    LogPrintf("RemoveLocal(%s)\n", addr);
    mapLocalHost.erase(addr);
    return true;
}

/** Make a particular network entirely off-limits (no automatic connects to it) */
void SetLimited(enum Network net, bool fLimited)
{
    if (net == NET_UNROUTABLE)
        return;
    LOCK(cs_mapLocalHost);
    vfLimited[net] = fLimited;
}

bool IsLimited(enum Network net)
{
    LOCK(cs_mapLocalHost);
    return vfLimited[net];
}

bool IsLimited(const CNetAddr& addr)
{
    return IsLimited(addr.GetNetwork());
}

/** vote for a local address */
bool SeenLocal(const CService& addr)
{
    {
        LOCK(cs_mapLocalHost);
        if (mapLocalHost.count(addr) == 0)
            return false;
        mapLocalHost[addr].nScore++;
    }
    return true;
}


/** check whether a given address is potentially local */
bool IsLocal(const CService& addr)
{
    LOCK(cs_mapLocalHost);
    return mapLocalHost.count(addr) > 0;
}

/** check whether a given network is one we can probably connect to */
bool IsReachable(enum Network net)
{
    LOCK(cs_mapLocalHost);
    return vfReachable[net] && !vfLimited[net];
}

/** check whether a given address is in a network we can probably connect to */
bool IsReachable(const CNetAddr& addr)
{
    enum Network net = addr.GetNetwork();
    return IsReachable(net);
}

// find 'best' local address for a particular peer
bool GetLocal(CService& addr, const CNetAddr* paddrPeer)
{
    if (!IsListening())
        return false;

    int nBestScore = -1;
    int nBestReachability = -1;
    {
        LOCK(cs_mapLocalHost);
        for (std::map<CNetAddr, LocalServiceInfo>::iterator it = mapLocalHost.begin(); it != mapLocalHost.end(); it++) {
            int nScore = (*it).second.nScore;
            int nReachability = (*it).first.GetReachabilityFrom(paddrPeer);
            if (nReachability > nBestReachability || (nReachability == nBestReachability && nScore > nBestScore)) {
                addr = CService((*it).first, (*it).second.nPort);
                nBestReachability = nReachability;
                nBestScore = nScore;
            }
        }
    }
    return nBestScore >= 0;
}

// get best local address for a particular peer as a CAddress
// Otherwise, return the unroutable 0.0.0.0 but filled in with
// the normal parameters, since the IP may be changed to a useful
// one by discovery.
CAddress GetLocalAddress(const CNetAddr* paddrPeer)
{
    CAddress ret(CService("0.0.0.0", GetListenPort()), 0);
    CService addr;
    if (GetLocal(addr, paddrPeer)) {
        ret = CAddress(addr);
    }
    ret.nServices = nLocalServices;
    ret.nTime = GetAdjustedTime();
    return ret;
}

unsigned short GetListenPort()
{
    return (unsigned short)(settings.GetArg("-port", Params().GetDefaultPort()));
}

const uint64_t& GetLocalServices()
{
    return nLocalServices;
}
void EnableBloomFilters()
{
    nLocalServices |= NODE_BLOOM;
}
bool BloomFiltersAreEnabled()
{
    return static_cast<bool>(nLocalServices & NODE_BLOOM);
}