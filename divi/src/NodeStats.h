#ifndef NODE_STATS_H
#define NODE_STATS_H
#include <stdint.h>
#include <string>
class CNode;
class CNodeStats
{
public:
    int nodeid;
    uint64_t nServices;
    int64_t nLastSend;
    int64_t nLastRecv;
    int64_t nTimeConnected;
    std::string addrName;
    int nVersion;
    std::string cleanSubVer;
    bool fInbound;
    int nStartingHeight;
    uint64_t nSendBytes;
    uint64_t nRecvBytes;
    bool fWhitelisted;
    double dPingTime;
    double dPingWait;
    std::string addrLocal;

    CNodeStats(CNode*);
};
#endif// NODE_STATS_H