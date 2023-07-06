#ifndef NETWORK_LOCAL_ADDRESS_HELPERS_H
#define NETWORK_LOCAL_ADDRESS_HELPERS_H
#include <netbase.h>
#include <protocol.h>
#include <string>

class CNode;

struct LocalServiceInfo;
enum {
    LOCAL_NONE,   // unknown
    LOCAL_IF,     // address a local interface listens on
    LOCAL_BIND,   // address explicit bound to
    LOCAL_UPNP,   // address reported by UPnP
    LOCAL_MANUAL, // address explicitly specified (-externalip=)

    LOCAL_MAX
};

int GetnScore(const CService& addr);
void SetLimited(enum Network net, bool fLimited = true);
bool IsLimited(enum Network net);
bool IsLimited(const CNetAddr& addr);
bool AddLocal(const CService& addr, int nScore = LOCAL_NONE);
bool AddLocal(const CNetAddr& addr, int nScore = LOCAL_NONE);
bool RemoveLocal(const CService& addr);
bool SeenLocal(const CService& addr);
bool IsLocal(const CService& addr);
bool GetLocal(CService& addr, const CNetAddr* paddrPeer = NULL);
bool IsReachable(enum Network net);
bool IsReachable(const CNetAddr& addr);
void SetReachable(enum Network net, bool fFlag = true);
CAddress GetLocalAddress(const CNetAddr* paddrPeer = NULL);
unsigned short GetListenPort();
const uint64_t& GetLocalServices();
void EnableBloomFilters();
bool BloomFiltersAreEnabled();
bool IsListening();
void setListeningFlag(bool updatedListenFlag);
bool isDiscoverEnabled();
void setDiscoverFlag(bool updatedDiscoverFlag);

struct LocalHostData
{
    std::string address;
    std::string port;
    std::string score;
    LocalHostData(const CNetAddr& addr, const LocalServiceInfo& info);
};
std::vector<LocalHostData> GetLocalHostData();
#endif// NETWORK_LOCAL_ADDRESS_HELPERS_H