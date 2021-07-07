#ifndef PEER_BANNING_SERVICE_H
#define PEER_BANNING_SERVICE_H
#include <map>
#include <string>
#include <sync.h>
#include <stdint.h>
#include <netbase.h>
class PeerBanningService
{
private:
    // Denial-of-service detection/prevention
    // Key is IP address, value is banned-until-time
    static std::map<CNetAddr, int64_t> setBanned;
    static CCriticalSection cs_setBanned;
    static int64_t defaultBanDuration;
public:
    // Denial-of-service detection/prevention
    // The idea is to detect peers that are behaving
    // badly and disconnect/ban them, but do it in a
    // one-coding-mistake-won't-shatter-the-entire-network
    // way.
    // IMPORTANT:  There should be nothing I can give a
    // node that it will forward on that will make that
    // node's peers drop it. If there is, an attacker
    // can isolate a node and/or try to split the network.
    // Dropping a node for sending stuff that is invalid
    // now but might be valid in a later version is also
    // dangerous, because it can cause a network split
    // between nodes running old code and nodes running
    // new code.
    static void SetDefaultBanDuration(int64_t banDuration);
    static void ClearBanned(); // needed for unit testing
    static std::string ListBanned();
    static bool IsBanned(int64_t currentTime, CNetAddr ip);
    static bool Ban(int64_t currentTime, const CNetAddr& ip);
    static bool Ban(const CNetAddr& addr, int64_t banTime);
    static bool LifetimeBan(const CNetAddr& addr);
};
#endif// PEER_BANNING_SERVICE_H