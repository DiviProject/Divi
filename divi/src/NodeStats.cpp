#include <NodeStats.h>

#include <Node.h>
#include <utiltime.h>
#undef X
#define X(name) name = pnode->name
CNodeStats::CNodeStats(CNode* pnode)
{
    nodeid = pnode->GetId();
    X(nServices);
    nLastSend = pnode->GetLastTimeDataSent();
    nLastRecv = pnode->GetLastTimeDataReceived();
    X(nTimeConnected);
    X(addrName);
    X(nVersion);
    X(cleanSubVer);
    X(fInbound);
    X(nStartingHeight);
    nSendBytes = pnode->GetSendBufferSize();
    nRecvBytes = pnode->GetTotalBytesReceived();
    X(fWhitelisted);

    // It is common for nodes with good ping times to suddenly become lagged,
    // due to a new block arriving or other large transfer.
    // Merely reporting pingtime might fool the caller into thinking the node was still responsive,
    // since pingtime does not update until the ping is complete, which might take a while.
    // So, if a ping is taking an unusually long time in flight,
    // the caller can immediately detect that this is happening.
    int64_t nPingUsecWait = 0;
    if ((0 != pnode->nPingNonceSent) && (0 != pnode->nPingUsecStart)) {
        nPingUsecWait = GetTimeMicros() - pnode->nPingUsecStart;
    }

    // Raw ping time is in microseconds, but show it to user as whole seconds (DIVI users should be well used to small numbers with many decimal places by now :)
    dPingTime = (((double)pnode->nPingUsecTime) / 1e6);
    dPingWait = (((double)nPingUsecWait) / 1e6);

    // Leave string empty if addrLocal invalid (not filled in yet)
    addrLocal = pnode->addrLocal.IsValid() ? pnode->addrLocal.ToString() : "";
}
#undef X