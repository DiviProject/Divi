// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/divi-config.h"
#endif

#include "net.h"

#include "addrman.h"
#include "chainparams.h"
#include "clientversion.h"
#include "miner.h"
#include "obfuscation.h"
#include "primitives/transaction.h"
#include "ui_interface.h"
#include <timeIntervalConstants.h>
#include <WalletBackupFeatureContainer.h>
#include "wallet.h"
#include "Settings.h"
#include <main.h>
#include <Logging.h>
#include <DataDirectory.h>
#include <utiltime.h>
#include <util.h>
#include <ThreadManagementHelpers.h>
#include <version.h>
#include <uiMessenger.h>
#include <PeerBanningService.h>
#include <AddrDB.h>

#ifdef WIN32
#include <string.h>
#else
#include <fcntl.h>
#endif

#ifdef USE_UPNP
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/miniwget.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
#endif

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

/** Time after which to disconnect, after waiting for a ping response (or inactivity). */
constexpr int TIMEOUT_INTERVAL = 20 * 60;
/** -listen default */
constexpr bool DEFAULT_LISTEN = true;
/** -upnp default */
#ifdef USE_UPNP
constexpr bool DEFAULT_UPNP = USE_UPNP;
#else
constexpr bool DEFAULT_UPNP = false;
#endif

#ifdef WIN32
// Win32 LevelDB doesn't use filedescriptors, and the ones used for
// accessing block files, don't count towards to fd_set size limit
// anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

// Dump addresses to peers.dat every 15 minutes (900s)
#define DUMP_ADDRESSES_INTERVAL 900

#if !defined(HAVE_MSG_NOSIGNAL) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

// Fix for ancient MinGW versions, that don't have defined these in ws2tcpip.h.
// Todo: Can be removed when our pull-tester is upgraded to a modern MinGW version.
#ifdef WIN32
#ifndef PROTECTION_LEVEL_UNRESTRICTED
#define PROTECTION_LEVEL_UNRESTRICTED 10
#endif
#ifndef IPV6_PROTECTION_LEVEL
#define IPV6_PROTECTION_LEVEL 23
#endif
#endif

using namespace boost;
using namespace std;
extern Settings& settings;
namespace
{
const int MAX_OUTBOUND_CONNECTIONS = 16;

struct ListenSocket {
    SOCKET socket;
    bool whitelisted;

    ListenSocket(SOCKET socket, bool whitelisted) : socket(socket), whitelisted(whitelisted) {}
};
}

//
// Global state variables
//
extern CWallet* pwalletMain;
bool fDiscover = true;
bool fListen = true;

static CNode* pnodeLocalHost = NULL;
static std::vector<ListenSocket> vhListenSocket;
CAddrMan addrman;
int nMaxConnections = 125;
bool fAddressesInitialized = false;

std::vector<CNode*> vNodes;
CCriticalSection cs_vNodes;
map<CInv, CDataStream> mapRelay;
deque<pair<int64_t, CInv> > vRelayExpiration;
CCriticalSection cs_mapRelay;

static deque<string> vOneShots;
CCriticalSection cs_vOneShots;

set<CNetAddr> setservAddNodeAddresses;
CCriticalSection cs_setservAddNodeAddresses;

std::vector<std::string> vAddedNodes;
CCriticalSection cs_vAddedNodes;

static CSemaphore* semOutbound = NULL;
boost::condition_variable messageHandlerCondition;

// Signals for message handling
static CNodeSignals globalNodeSignals;
CNodeSignals& GetNodeSignals()
{
    return globalNodeSignals;
}

int GetMaxConnections()
{
    return nMaxConnections;
}

CAddrMan& GetNetworkAddressManager()
{
    return addrman;
}

void AddOneShot(string strDest)
{
    LOCK(cs_vOneShots);
    vOneShots.push_back(strDest);
}

bool RecvLine(SOCKET hSocket, string& strLine)
{
    strLine = "";
    while (true) {
        char c;
        int nBytes = recv(hSocket, &c, 1, 0);
        if (nBytes > 0) {
            if (c == '\n')
                continue;
            if (c == '\r')
                return true;
            strLine += c;
            if (strLine.size() >= 9000)
                return true;
        } else if (nBytes <= 0) {
            boost::this_thread::interruption_point();
            if (nBytes < 0) {
                int nErr = WSAGetLastError();
                if (nErr == WSAEMSGSIZE)
                    continue;
                if (nErr == WSAEWOULDBLOCK || nErr == WSAEINTR || nErr == WSAEINPROGRESS) {
                    MilliSleep(10);
                    continue;
                }
            }
            if (!strLine.empty())
                return true;
            if (nBytes == 0) {
                // socket closed
                LogPrint("net", "socket closed\n");
                return false;
            } else {
                // socket error
                int nErr = WSAGetLastError();
                LogPrint("net", "recv failed: %s\n", NetworkErrorString(nErr));
                return false;
            }
        }
    }
}

// Is our peer's addrLocal potentially useful as an external IP source?
bool IsPeerAddrLocalGood(CNode* pnode)
{
    return fDiscover && pnode->addr.IsRoutable() && pnode->addrLocal.IsRoutable() &&
           !IsLimited(pnode->addrLocal.GetNetwork());
}

// pushes our own address to a peer
void AdvertizeLocal(CNode* pnode)
{
    if (fListen && pnode->fSuccessfullyConnected) {
        CAddress addrLocal = GetLocalAddress(&pnode->addr);
        // If discovery is enabled, sometimes give our peer the address it
        // tells us that it sees us as in case it has a better idea of our
        // address than we do.
        if (IsPeerAddrLocalGood(pnode) && (!addrLocal.IsRoutable() ||
                                              GetRand((GetnScore(addrLocal) > LOCAL_MANUAL) ? 8 : 2) == 0)) {
            addrLocal.SetIP(pnode->addrLocal);
        }
        if (addrLocal.IsRoutable()) {
            LogPrintf("AdvertizeLocal: advertizing address %s\n", addrLocal);
            pnode->PushAddress(addrLocal);
        }
    }
}

void AddressCurrentlyConnected(const CService& addr)
{
    addrman.Connected(addr);
}

CNode* FindNode(const CNetAddr& ip)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes)
        if ((CNetAddr)pnode->addr == ip)
            return (pnode);
    return NULL;
}

CNode* FindNode(const std::string& addrName)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes)
        if (pnode->addrName == addrName)
            return (pnode);
    return NULL;
}

CNode* FindNode(const CService& addr)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (Params().NetworkID() == CBaseChainParams::REGTEST) {
            //if using regtest, just check the IP
            if ((CNetAddr)pnode->addr == (CNetAddr)addr)
                return (pnode);
        } else {
            if (pnode->addr == addr)
                return (pnode);
        }
    }
    return NULL;
}

CNode* ConnectNode(CAddress addrConnect, const char* pszDest, bool obfuScationMaster)
{
    if (pszDest == NULL) {
        // we clean masternode connections in CMasternodeMan::ProcessMasternodeConnections()
        // so should be safe to skip this and connect to local Hot MN on CActiveMasternode::ManageStatus()
        if (IsLocal(addrConnect) && !obfuScationMaster)
            return NULL;

        // Look for an existing connection
        CNode* pnode = FindNode((CService)addrConnect);
        if (pnode) {
            pnode->fObfuScationMaster = obfuScationMaster;

            pnode->AddRef();
            return pnode;
        }
    }

    /// debug print
    LogPrint("net", "trying connection %s lastseen=%.1fhrs\n",
        pszDest ? pszDest : addrConnect.ToString(),
        pszDest ? 0.0 : (double)(GetAdjustedTime() - addrConnect.nTime) / 3600.0);

    // Connect
    SOCKET hSocket;
    bool proxyConnectionFailed = false;
    if (pszDest ? ConnectSocketByName(addrConnect, hSocket, pszDest, Params().GetDefaultPort(), nConnectTimeout, &proxyConnectionFailed) :
                  ConnectSocket(addrConnect, hSocket, nConnectTimeout, &proxyConnectionFailed)) {
        if (!IsSelectableSocket(hSocket)) {
            LogPrintf("Cannot create connection: non-selectable socket created (fd >= FD_SETSIZE ?)\n");
            CloseSocket(hSocket);
            return NULL;
        }

        addrman.Attempt(addrConnect);

        // Add node
        CNode* pnode = new CNode(&GetNodeSignals(),hSocket, addrConnect, pszDest ? pszDest : "", false);
        pnode->AddRef();

        {
            LOCK(cs_vNodes);
            vNodes.push_back(pnode);
        }

        pnode->nTimeConnected = GetTime();
        if (obfuScationMaster) pnode->fObfuScationMaster = true;

        return pnode;
    } else if (!proxyConnectionFailed) {
        // If connecting to the node failed, and failure is not caused by a problem connecting to
        // the proxy, mark this as an attempt.
        addrman.Attempt(addrConnect);
    }

    return NULL;
}

static list<CNode*> vNodesDisconnected;
static std::vector<CSubNet> vWhitelistedRange;
static CCriticalSection cs_vWhitelistedRange;
bool IsWhitelistedRange(const CNetAddr& addr)
{
    LOCK(cs_vWhitelistedRange);
    for(const CSubNet& subnet: vWhitelistedRange)
    {
        if (subnet.Match(addr))
            return true;
    }
    return false;
}

void AddWhitelistedRange(const CSubNet& subnet)
{
    LOCK(cs_vWhitelistedRange);
    vWhitelistedRange.push_back(subnet);
}

void ThreadSocketHandler()
{
    unsigned int nPrevNodeCount = 0;
    while (true) {
        //
        // Disconnect nodes
        //
        {
            LOCK(cs_vNodes);
            // Disconnect unused nodes
            std::vector<CNode*> vNodesCopy = vNodes;
            BOOST_FOREACH (CNode* pnode, vNodesCopy) {
                if (pnode->fDisconnect ||
                    (pnode->GetRefCount() <= 0 && pnode->vRecvMsg.empty() && pnode->nSendSize == 0 && pnode->ssSend.empty())) {
                    // remove from vNodes
                    vNodes.erase(remove(vNodes.begin(), vNodes.end(), pnode), vNodes.end());

                    // release outbound grant (if any)
                    pnode->grantOutbound.Release();

                    // close socket and cleanup
                    pnode->CloseSocketDisconnect();

                    // hold in disconnected pool until all refs are released
                    if (pnode->fNetworkNode || pnode->fInbound)
                        pnode->Release();
                    vNodesDisconnected.push_back(pnode);
                }
            }
        }
        {
            // Delete disconnected nodes
            list<CNode*> vNodesDisconnectedCopy = vNodesDisconnected;
            BOOST_FOREACH (CNode* pnode, vNodesDisconnectedCopy) {
                // wait until threads are done using it
                if (pnode->GetRefCount() <= 0) {
                    bool fDelete = false;
                    {
                        TRY_LOCK(pnode->cs_vSend, lockSend);
                        if (lockSend) {
                            TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                            if (lockRecv) {
                                TRY_LOCK(pnode->cs_inventory, lockInv);
                                if (lockInv)
                                    fDelete = true;
                            }
                        }
                    }
                    if (fDelete) {
                        vNodesDisconnected.remove(pnode);
                        delete pnode;
                    }
                }
            }
        }
        size_t vNodesSize;
        {
            LOCK(cs_vNodes);
            vNodesSize = vNodes.size();
        }
        if(vNodesSize != nPrevNodeCount) {
            nPrevNodeCount = vNodesSize;
            uiInterface.NotifyNumConnectionsChanged(nPrevNodeCount);
        }

        //
        // Find which sockets have data to receive
        //
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000; // frequency to poll pnode->vSend

        fd_set fdsetRecv;
        fd_set fdsetSend;
        fd_set fdsetError;
        FD_ZERO(&fdsetRecv);
        FD_ZERO(&fdsetSend);
        FD_ZERO(&fdsetError);
        SOCKET hSocketMax = 0;
        bool have_fds = false;

        BOOST_FOREACH (const ListenSocket& hListenSocket, vhListenSocket) {
            FD_SET(hListenSocket.socket, &fdsetRecv);
            hSocketMax = max(hSocketMax, hListenSocket.socket);
            have_fds = true;
        }

        {
            LOCK(cs_vNodes);
            BOOST_FOREACH (CNode* pnode, vNodes) {
                if (pnode->hSocket == INVALID_SOCKET)
                    continue;
                FD_SET(pnode->hSocket, &fdsetError);
                hSocketMax = max(hSocketMax, pnode->hSocket);
                have_fds = true;

                // Implement the following logic:
                // * If there is data to send, select() for sending data. As this only
                //   happens when optimistic write failed, we choose to first drain the
                //   write buffer in this case before receiving more. This avoids
                //   needlessly queueing received data, if the remote peer is not themselves
                //   receiving data. This means properly utilizing TCP flow control signalling.
                // * Otherwise, if there is no (complete) message in the receive buffer,
                //   or there is space left in the buffer, select() for receiving data.
                // * (if neither of the above applies, there is certainly one message
                //   in the receiver buffer ready to be processed).
                // Together, that means that at least one of the following is always possible,
                // so we don't deadlock:
                // * We send some data.
                // * We wait for data to be received (and disconnect after timeout).
                // * We process a message in the buffer (message handler thread).
                {
                    TRY_LOCK(pnode->cs_vSend, lockSend);
                    if (lockSend && !pnode->vSendMsg.empty()) {
                        FD_SET(pnode->hSocket, &fdsetSend);
                        continue;
                    }
                }
                {
                    TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                    if (lockRecv &&
                        (pnode->vRecvMsg.empty() ||
                        !pnode->vRecvMsg.front().complete() ||
                        pnode->IsAvailableToReceive()))
                    {
                        FD_SET(pnode->hSocket, &fdsetRecv);
                    }
                }
            }
        }

        int nSelect = select(have_fds ? hSocketMax + 1 : 0,
            &fdsetRecv, &fdsetSend, &fdsetError, &timeout);
        boost::this_thread::interruption_point();

        if (nSelect == SOCKET_ERROR) {
            if (have_fds) {
                int nErr = WSAGetLastError();
                LogPrintf("socket select error %s\n", NetworkErrorString(nErr));
                for (unsigned int i = 0; i <= hSocketMax; i++)
                    FD_SET(i, &fdsetRecv);
            }
            FD_ZERO(&fdsetSend);
            FD_ZERO(&fdsetError);
            MilliSleep(timeout.tv_usec / 1000);
        }

        //
        // Accept new connections
        //
        BOOST_FOREACH (const ListenSocket& hListenSocket, vhListenSocket) {
            if (hListenSocket.socket != INVALID_SOCKET && FD_ISSET(hListenSocket.socket, &fdsetRecv)) {
                struct sockaddr_storage sockaddr;
                socklen_t len = sizeof(sockaddr);
                SOCKET hSocket = accept(hListenSocket.socket, (struct sockaddr*)&sockaddr, &len);
                CAddress addr;
                int nInbound = 0;

                if (hSocket != INVALID_SOCKET)
                    if (!addr.SetSockAddr((const struct sockaddr*)&sockaddr))
                        LogPrintf("Warning: Unknown socket family\n");

                bool whitelisted = hListenSocket.whitelisted || IsWhitelistedRange(addr);
                {
                    LOCK(cs_vNodes);
                    BOOST_FOREACH (CNode* pnode, vNodes)
                        if (pnode->fInbound)
                            nInbound++;
                }

                if (hSocket == INVALID_SOCKET) {
                    int nErr = WSAGetLastError();
                    if (nErr != WSAEWOULDBLOCK)
                        LogPrintf("socket error accept failed: %s\n", NetworkErrorString(nErr));
                } else if (!IsSelectableSocket(hSocket)) {
                    LogPrintf("connection from %s dropped: non-selectable socket\n", addr);
                    CloseSocket(hSocket);
                } else if (nInbound >= nMaxConnections - MAX_OUTBOUND_CONNECTIONS) {
                    LogPrint("net", "connection from %s dropped (full)\n", addr);
                    CloseSocket(hSocket);
                } else if (PeerBanningService::IsBanned(GetTime(),addr) && !whitelisted) {
                    LogPrintf("connection from %s dropped (banned)\n", addr);
                    CloseSocket(hSocket);
                } else {
                    CNode* pnode = new CNode(&GetNodeSignals(),hSocket, addr, "", true);
                    pnode->AddRef();
                    pnode->fWhitelisted = whitelisted;

                    {
                        LOCK(cs_vNodes);
                        vNodes.push_back(pnode);
                    }
                }
            }
        }

        //
        // Service each socket
        //
        std::vector<CNode*> vNodesCopy;
        {
            LOCK(cs_vNodes);
            vNodesCopy = vNodes;
            BOOST_FOREACH (CNode* pnode, vNodesCopy)
                pnode->AddRef();
        }
        BOOST_FOREACH (CNode* pnode, vNodesCopy) {
            boost::this_thread::interruption_point();

            //
            // Receive
            //
            if (pnode->hSocket == INVALID_SOCKET)
                continue;
            if (FD_ISSET(pnode->hSocket, &fdsetRecv) || FD_ISSET(pnode->hSocket, &fdsetError)) {
                TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                if (lockRecv) {
                    {
                        // typical socket buffer is 8K-64K
                        char pchBuf[0x10000];
                        int nBytes = recv(pnode->hSocket, pchBuf, sizeof(pchBuf), MSG_DONTWAIT);
                        if (nBytes > 0) {
                            if (!pnode->ReceiveMsgBytes(pchBuf, nBytes,messageHandlerCondition))
                                pnode->CloseSocketDisconnect();
                            pnode->nLastRecv = GetTime();
                            pnode->nRecvBytes += nBytes;
                            NetworkUsageStats::RecordBytesRecv(nBytes);
                        } else if (nBytes == 0) {
                            // socket closed gracefully
                            if (!pnode->fDisconnect)
                                LogPrint("net", "socket closed\n");
                            pnode->CloseSocketDisconnect();
                        } else if (nBytes < 0) {
                            // error
                            int nErr = WSAGetLastError();
                            if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS) {
                                if (!pnode->fDisconnect)
                                    LogPrintf("socket recv error %s\n", NetworkErrorString(nErr));
                                pnode->CloseSocketDisconnect();
                            }
                        }
                    }
                }
            }

            //
            // Send
            //
            if (pnode->hSocket == INVALID_SOCKET)
                continue;
            if (FD_ISSET(pnode->hSocket, &fdsetSend)) {
                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (lockSend)
                    pnode->SocketSendData();
            }

            //
            // Inactivity checking
            //
            int64_t nTime = GetTime();
            if (nTime - pnode->nTimeConnected > 60) {
                if (pnode->nLastRecv == 0 || pnode->nLastSend == 0) {
                    LogPrint("net", "socket no message in first 60 seconds, %d %d from %d\n", pnode->nLastRecv != 0, pnode->nLastSend != 0, pnode->id);
                    pnode->fDisconnect = true;
                } else if (nTime - pnode->nLastSend > TIMEOUT_INTERVAL) {
                    LogPrintf("socket sending timeout: %is\n", nTime - pnode->nLastSend);
                    pnode->fDisconnect = true;
                } else if (nTime - pnode->nLastRecv > (pnode->nVersion > BIP0031_VERSION ? TIMEOUT_INTERVAL : 90 * 60)) {
                    LogPrintf("socket receive timeout: %is\n", nTime - pnode->nLastRecv);
                    pnode->fDisconnect = true;
                } else if (pnode->nPingNonceSent && pnode->nPingUsecStart + TIMEOUT_INTERVAL * 1000000 < GetTimeMicros()) {
                    LogPrintf("ping timeout: %fs\n", 0.000001 * (GetTimeMicros() - pnode->nPingUsecStart));
                    pnode->fDisconnect = true;
                }
            }
        }
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH (CNode* pnode, vNodesCopy)
                pnode->Release();
        }
    }
}


#ifdef USE_UPNP
void ThreadMapPort()
{
    std::string port = strprintf("%u", GetListenPort());
    const char* multicastif = 0;
    const char* minissdpdpath = 0;
    struct UPNPDev* devlist = 0;
    char lanaddr[64];

#ifndef UPNPDISCOVER_SUCCESS
    /* miniupnpc 1.5 */
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0);
#elif MINIUPNPC_API_VERSION < 14
    /* miniupnpc 1.6 */
    int error = 0;
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, &error);
#else
    /* miniupnpc 1.9.20150730 */
    int error = 0;
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, 2, &error);
#endif

    struct UPNPUrls urls;
    struct IGDdatas data;
    int r;

    r = UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr));
    if (r == 1) {
        if (fDiscover) {
            char externalIPAddress[40];
            r = UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, externalIPAddress);
            if (r != UPNPCOMMAND_SUCCESS)
                LogPrintf("UPnP: GetExternalIPAddress() returned %d\n", r);
            else {
                if (externalIPAddress[0]) {
                    LogPrintf("UPnP: ExternalIPAddress = %s\n", externalIPAddress);
                    AddLocal(CNetAddr(externalIPAddress), LOCAL_UPNP);
                } else
                    LogPrintf("UPnP: GetExternalIPAddress failed.\n");
            }
        }

        string strDesc = "DIVI " + FormatFullVersion();

        try {
            while (true) {
#ifndef UPNPDISCOVER_SUCCESS
                /* miniupnpc 1.5 */
                r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                    port.c_str(), port.c_str(), lanaddr, strDesc.c_str(), "TCP", 0);
#else
                /* miniupnpc 1.6 */
                r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                    port.c_str(), port.c_str(), lanaddr, strDesc.c_str(), "TCP", 0, "0");
#endif

                if (r != UPNPCOMMAND_SUCCESS)
                    LogPrintf("AddPortMapping(%s, %s, %s) failed with code %d (%s)\n",
                        port, port, lanaddr, r, strupnperror(r));
                else
                    LogPrintf("UPnP Port Mapping successful.\n");
                ;

                MilliSleep(20 * 60 * 1000); // Refresh every 20 minutes
            }
        } catch (boost::thread_interrupted) {
            r = UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, port.c_str(), "TCP", 0);
            LogPrintf("UPNP_DeletePortMapping() returned : %d\n", r);
            freeUPNPDevlist(devlist);
            devlist = 0;
            FreeUPNPUrls(&urls);
            throw;
        }
    } else {
        LogPrintf("No valid UPnP IGDs found\n");
        freeUPNPDevlist(devlist);
        devlist = 0;
        if (r != 0)
            FreeUPNPUrls(&urls);
    }
}

void MapPort(bool fUseUPnP)
{
    static boost::thread* upnp_thread = NULL;

    if (fUseUPnP) {
        if (upnp_thread) {
            upnp_thread->interrupt();
            upnp_thread->join();
            delete upnp_thread;
        }
        upnp_thread = new boost::thread(boost::bind(&TraceThread<void (*)()>, "upnp", &ThreadMapPort));
    } else if (upnp_thread) {
        upnp_thread->interrupt();
        upnp_thread->join();
        delete upnp_thread;
        upnp_thread = NULL;
    }
}

#else
void MapPort(bool)
{
    // Intentionally left blank.
}
#endif


void ThreadDNSAddressSeed()
{
    // goal: only query DNS seeds if address need is acute
    if ((addrman.size() > 0) &&
        (!settings.GetBoolArg("-forcednsseed", false))) {
        MilliSleep(11 * 1000);

        LOCK(cs_vNodes);
        if (vNodes.size() >= 2) {
            LogPrintf("P2P peers available. Skipped DNS seeding.\n");
            return;
        }
    }

    const std::vector<CDNSSeedData>& vSeeds = Params().DNSSeeds();
    int found = 0;

    LogPrintf("Loading addresses from DNS seeds (could take a while)\n");

    BOOST_FOREACH (const CDNSSeedData& seed, vSeeds) {
        if (HaveNameProxy()) {
            AddOneShot(seed.host);
        } else {
            std::vector<CNetAddr> vIPs;
            std::vector<CAddress> vAdd;
            if (LookupHost(seed.host.c_str(), vIPs)) {
                BOOST_FOREACH (CNetAddr& ip, vIPs) {
                    int nOneDay = 24 * 3600;
                    CAddress addr = CAddress(CService(ip, Params().GetDefaultPort()));
                    addr.nTime = GetTime() - 3 * nOneDay - GetRand(4 * nOneDay); // use a random age between 3 and 7 days old
                    vAdd.push_back(addr);
                    found++;
                }
            }
            addrman.Add(vAdd, CNetAddr(seed.name, true));
        }
    }

    LogPrintf("%d addresses found from DNS seeds\n", found);
}


void DumpAddresses()
{
    int64_t nStart = GetTimeMillis();

    CAddrDB adb;
    adb.Write(addrman);

    LogPrint("net", "Flushed %d addresses to peers.dat  %dms\n",
        addrman.size(), GetTimeMillis() - nStart);
}

void static ProcessOneShot()
{
    string strDest;
    {
        LOCK(cs_vOneShots);
        if (vOneShots.empty())
            return;
        strDest = vOneShots.front();
        vOneShots.pop_front();
    }
    CAddress addr;
    CSemaphoreGrant grant(*semOutbound, true);
    if (grant) {
        if (!OpenNetworkConnection(addr, &grant, strDest.c_str(), true))
            AddOneShot(strDest);
    }
}

void ThreadOpenConnections()
{
    // Connect to specific addresses
    if (settings.ParameterIsSet("-connect")) {
        const auto& connections = settings.GetMultiParameter("-connect");
        if(connections.size() > 0)
        {
            for (int64_t nLoop = 0;; nLoop++)
            {
                ProcessOneShot();
                BOOST_FOREACH (string strAddr, connections) {
                    CAddress addr;
                    OpenNetworkConnection(addr, NULL, strAddr.c_str());
                    for (int i = 0; i < 10 && i < nLoop; i++) {
                        MilliSleep(500);
                    }
                }
                MilliSleep(500);
            }
        }
    }

    // Initiate network connections
    int64_t nStart = GetTime();
    while (true) {
        ProcessOneShot();

        MilliSleep(500);

        CSemaphoreGrant grant(*semOutbound);
        boost::this_thread::interruption_point();

        // Add seed nodes if DNS seeds are all down (an infrastructure attack?).
        if (addrman.size() == 0 && (GetTime() - nStart > 60)) {
            static bool done = false;
            if (!done) {
                LogPrintf("Adding fixed seed nodes as DNS doesn't seem to be available.\n");
                addrman.Add(Params().FixedSeeds(), CNetAddr("127.0.0.1"));
                done = true;
            }
        }

        //
        // Choose an address to connect to based on most recently seen
        //
        CAddress addrConnect;

        // Only connect out to one peer per network group (/16 for IPv4).
        // Do this here so we don't have to critsect vNodes inside mapAddresses critsect.
        int nOutbound = 0;
        std::set<std::vector<unsigned char> > setConnected;
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH (CNode* pnode, vNodes) {
                if (!pnode->fInbound) {
                    setConnected.insert(pnode->addr.GetGroup());
                    nOutbound++;
                }
            }
        }

        int64_t nANow = GetAdjustedTime();

        int nTries = 0;
        while (true) {
            CAddress addr = addrman.Select();

            // if we selected an invalid address, restart
            if (!addr.IsValid() || setConnected.count(addr.GetGroup()) || IsLocal(addr))
                break;

            // If we didn't find an appropriate destination after trying 100 addresses fetched from addrman,
            // stop this loop, and let the outer loop run again (which sleeps, adds seed nodes, recalculates
            // already-connected network ranges, ...) before trying new addrman addresses.
            nTries++;
            if (nTries > 100)
                break;

            if (IsLimited(addr))
                continue;

            // only consider very recently tried nodes after 30 failed attempts
            if (nANow - addr.nLastTry < 600 && nTries < 30)
                continue;

            // do not allow non-default ports, unless after 50 invalid addresses selected already
            if (addr.GetPort() != Params().GetDefaultPort() && nTries < 50)
                continue;

            addrConnect = addr;
            break;
        }

        if (addrConnect.IsValid())
            OpenNetworkConnection(addrConnect, &grant);
    }
}

void ThreadOpenAddedConnections()
{
    {
        LOCK(cs_vAddedNodes);
        vAddedNodes = settings.GetMultiParameter("-addnode");
    }

    if (HaveNameProxy()) {
        while (true) {
            list<string> lAddresses(0);
            {
                LOCK(cs_vAddedNodes);
                BOOST_FOREACH (string& strAddNode, vAddedNodes)
                    lAddresses.push_back(strAddNode);
            }
            BOOST_FOREACH (string& strAddNode, lAddresses) {
                CAddress addr;
                CSemaphoreGrant grant(*semOutbound);
                OpenNetworkConnection(addr, &grant, strAddNode.c_str());
                MilliSleep(500);
            }
            MilliSleep(120000); // Retry every 2 minutes
        }
    }

    for (unsigned int i = 0; true; i++) {
        list<string> lAddresses(0);
        {
            LOCK(cs_vAddedNodes);
            BOOST_FOREACH (string& strAddNode, vAddedNodes)
                lAddresses.push_back(strAddNode);
        }

        std::list<std::vector<CService> > lservAddressesToAdd(0);
        BOOST_FOREACH (std::string& strAddNode, lAddresses) {
            std::vector<CService> vservNode(0);
            if (Lookup(strAddNode.c_str(), vservNode, Params().GetDefaultPort(), fNameLookup, 0)) {
                lservAddressesToAdd.push_back(vservNode);
                {
                    LOCK(cs_setservAddNodeAddresses);
                    BOOST_FOREACH (CService& serv, vservNode)
                        setservAddNodeAddresses.insert(serv);
                }
            }
        }
        // Attempt to connect to each IP for each addnode entry until at least one is successful per addnode entry
        // (keeping in mind that addnode entries can have many IPs if fNameLookup)
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH (CNode* pnode, vNodes)
                for (std::list<std::vector<CService> >::iterator it = lservAddressesToAdd.begin(); it != lservAddressesToAdd.end(); it++)
                    BOOST_FOREACH (CService& addrNode, *(it))
                        if (pnode->addr == addrNode) {
                            it = lservAddressesToAdd.erase(it);
                            it--;
                            break;
                        }
        }
        BOOST_FOREACH (std::vector<CService>& vserv, lservAddressesToAdd) {
            CSemaphoreGrant grant(*semOutbound);
            OpenNetworkConnection(CAddress(vserv[i % vserv.size()]), &grant);
            MilliSleep(500);
        }
        MilliSleep(120000); // Retry every 2 minutes
    }
}

// if successful, this moves the passed grant to the constructed node
bool OpenNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant* grantOutbound, const char* pszDest, bool fOneShot)
{
    //
    // Initiate outbound network connection
    //
    boost::this_thread::interruption_point();
    if (!pszDest) {
        if (IsLocal(addrConnect) ||
            FindNode((CNetAddr)addrConnect) || PeerBanningService::IsBanned(GetTime(),addrConnect) ||
            FindNode(addrConnect.ToStringIPPort()))
            return false;
    } else if (FindNode(pszDest))
        return false;

    CNode* pnode = ConnectNode(addrConnect, pszDest);
    boost::this_thread::interruption_point();

    if (!pnode)
        return false;
    if (grantOutbound)
        grantOutbound->MoveTo(pnode->grantOutbound);
    pnode->fNetworkNode = true;
    if (fOneShot)
        pnode->fOneShot = true;

    return true;
}


void ThreadMessageHandler()
{
    boost::mutex condition_mutex;
    boost::unique_lock<boost::mutex> lock(condition_mutex);
    static CNodeSignals& nodeSignals = GetNodeSignals();

    /* We periodically rebroadcast our address.  This is the last time
       we did a broadcast.  */
    int64_t nLastRebroadcast = 0;

    SetThreadPriority(THREAD_PRIORITY_BELOW_NORMAL);
    while (true) {
        std::vector<CNode*> vNodesCopy;
        {
            LOCK(cs_vNodes);
            vNodesCopy = vNodes;
            BOOST_FOREACH (CNode* pnode, vNodesCopy) {
                pnode->AddRef();
            }
        }

        bool rebroadcast = (!IsInitialBlockDownload() && (GetTime() > nLastRebroadcast + 24 * 60 * 60));

        // Poll the connected nodes for messages
        CNode* pnodeTrickle = NULL;
        if (!vNodesCopy.empty())
            pnodeTrickle = vNodesCopy[GetRand(vNodesCopy.size())];

        bool fSleep = true;

        BOOST_FOREACH (CNode* pnode, vNodesCopy) {
            if (pnode->fDisconnect)
                continue;

            // Receive messages
            {
                TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                if (lockRecv) {
                    if (!nodeSignals.ProcessMessages(pnode))
                        pnode->CloseSocketDisconnect();

                    if (pnode->GetSendBufferStatus()==NodeBufferStatus::HAS_SPACE)
                    {
                        if (!pnode->vRecvGetData.empty() || (!pnode->vRecvMsg.empty() && pnode->vRecvMsg[0].complete())) {
                            fSleep = false;
                        }
                    }
                }
            }
            boost::this_thread::interruption_point();

            // Handle potential ping messages first.
            if (pnode->CanSendMessagesToPeer())
            {
                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (lockSend)
                    pnode->MaybeSendPing();
            }
            boost::this_thread::interruption_point();

            // Rebroadcasting is done for all nodes after the ping
            // of the first; this is to mimic previous behaviour.
            if (rebroadcast) {
                for (auto* pnode2 : vNodesCopy) {
                    TRY_LOCK(pnode2->cs_vSend, lockSend);
                    if (!lockSend)
                        continue;

                    // Periodically clear setAddrKnown to allow refresh broadcasts
                    if (nLastRebroadcast > 0)
                        pnode2->setAddrKnown.clear();

                    // Rebroadcast our address
                    AdvertizeLocal(pnode2);
                    boost::this_thread::interruption_point();
                }

                nLastRebroadcast = GetTime();
                rebroadcast = false;
            }

            // Send messages
            if (pnode->CanSendMessagesToPeer())
            {
                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (lockSend)
                    nodeSignals.SendMessages(pnode, pnode == pnodeTrickle || pnode->fWhitelisted);
            }
            boost::this_thread::interruption_point();
        }


        {
            LOCK(cs_vNodes);
            BOOST_FOREACH (CNode* pnode, vNodesCopy)
                pnode->Release();
        }

        if (fSleep)
            messageHandlerCondition.timed_wait(lock, boost::posix_time::microsec_clock::universal_time() + boost::posix_time::milliseconds(100));
    }
}

void ThreadBackupWallet(const CWallet* wallet)
{
    const std::string& walletFileName = wallet->strWalletFile;
    static WalletBackupFeatureContainer walletBackupFeatureContainer(static_cast<int>(settings.GetArg("-monthlybackups", 12)), walletFileName, GetDataDir().string());
    while (true)
    {
        if(!wallet->fFileBacked) return;

        {
            LOCK(walletBackupFeatureContainer.GetDatabase().GetDatabaseLock());
            if (!walletBackupFeatureContainer.GetDatabase().FilenameIsInUse(walletFileName))
            {
                // Flush log data to the dat file
                walletBackupFeatureContainer.GetDatabase().Dettach(walletFileName);
                LogPrintf("backing up wallet\n");
                if(walletBackupFeatureContainer.GetWalletIntegrityVerifier().CheckWalletIntegrity(GetDataDir().string(), walletFileName))
                {
                    walletBackupFeatureContainer.GetMonthlyBackupCreator().BackupWallet();
                    return;
                }
                else
                {
                    LogPrintf("Error: Wallet integrity check failed.");
                    return;
                }
            }
        }
        MilliSleep(100);
    }
}

bool BindListenPort(const CService& addrBind, string& strError, bool fWhitelisted)
{
    strError = "";
    int nOne = 1;

    // Create socket for listening for incoming connections
    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    if (!addrBind.GetSockAddr((struct sockaddr*)&sockaddr, &len)) {
        strError = strprintf("Error: Bind address family for %s not supported", addrBind.ToString());
        LogPrintf("%s\n", strError);
        return false;
    }

    SOCKET hListenSocket = socket(((struct sockaddr*)&sockaddr)->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (hListenSocket == INVALID_SOCKET) {
        strError = strprintf("Error: Couldn't open socket for incoming connections (socket returned error %s)", NetworkErrorString(WSAGetLastError()));
        LogPrintf("%s\n", strError);
        return false;
    }
    if (!IsSelectableSocket(hListenSocket)) {
        strError = "Error: Couldn't create a listenable socket for incoming connections";
        LogPrintf("%s\n", strError);
        return false;
    }


#ifndef WIN32
#ifdef SO_NOSIGPIPE
    // Different way of disabling SIGPIPE on BSD
    setsockopt(hListenSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&nOne, sizeof(int));
#endif
    // Allow binding if the port is still in TIME_WAIT state after
    // the program was closed and restarted. Not an issue on windows!
    setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (void*)&nOne, sizeof(int));
#endif

    // Set to non-blocking, incoming connections will also inherit this
    if (!SetSocketNonBlocking(hListenSocket, true)) {
        strError = strprintf("BindListenPort: Setting listening socket to non-blocking failed, error %s\n", NetworkErrorString(WSAGetLastError()));
        LogPrintf("%s\n", strError);
        return false;
    }

    // some systems don't have IPV6_V6ONLY but are always v6only; others do have the option
    // and enable it by default or not. Try to enable it, if possible.
    if (addrBind.IsIPv6()) {
#ifdef IPV6_V6ONLY
#ifdef WIN32
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&nOne, sizeof(int));
#else
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (void*)&nOne, sizeof(int));
#endif
#endif
#ifdef WIN32
        int nProtLevel = PROTECTION_LEVEL_UNRESTRICTED;
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_PROTECTION_LEVEL, (const char*)&nProtLevel, sizeof(int));
#endif
    }

    if (::bind(hListenSocket, (struct sockaddr*)&sockaddr, len) == SOCKET_ERROR) {
        int nErr = WSAGetLastError();
        if (nErr == WSAEADDRINUSE)
            strError = strprintf(translate("Unable to bind to %s on this computer. DIVI Core is probably already running."), addrBind.ToString());
        else
            strError = strprintf(translate("Unable to bind to %s on this computer (bind returned error %s)"), addrBind.ToString(), NetworkErrorString(nErr));
        LogPrintf("%s\n", strError);
        CloseSocket(hListenSocket);
        return false;
    }
    LogPrintf("Bound to %s\n", addrBind);

    // Listen for incoming connections
    if (listen(hListenSocket, SOMAXCONN) == SOCKET_ERROR) {
        strError = strprintf(translate("Error: Listening for incoming connections failed (listen returned error %s)"), NetworkErrorString(WSAGetLastError()));
        LogPrintf("%s\n", strError);
        CloseSocket(hListenSocket);
        return false;
    }

    vhListenSocket.push_back(ListenSocket(hListenSocket, fWhitelisted));

    if (addrBind.IsRoutable() && fDiscover && !fWhitelisted)
        AddLocal(addrBind, LOCAL_BIND);

    return true;
}

void static Discover(boost::thread_group& threadGroup)
{
    if (!fDiscover)
        return;

#ifdef WIN32
    // Get local host IP
    char pszHostName[256] = "";
    if (gethostname(pszHostName, sizeof(pszHostName)) != SOCKET_ERROR) {
        std::vector<CNetAddr> vaddr;
        if (LookupHost(pszHostName, vaddr)) {
            BOOST_FOREACH (const CNetAddr& addr, vaddr) {
                if (AddLocal(addr, LOCAL_IF))
                    LogPrintf("%s: %s - %s\n", __func__, pszHostName, addr);
            }
        }
    }
#else
    // Get local host ip
    struct ifaddrs* myaddrs;
    if (getifaddrs(&myaddrs) == 0) {
        for (struct ifaddrs* ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL) continue;
            if ((ifa->ifa_flags & IFF_UP) == 0) continue;
            if (strcmp(ifa->ifa_name, "lo") == 0) continue;
            if (strcmp(ifa->ifa_name, "lo0") == 0) continue;
            if (ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in* s4 = (struct sockaddr_in*)(ifa->ifa_addr);
                CNetAddr addr(s4->sin_addr);
                if (AddLocal(addr, LOCAL_IF))
                    LogPrintf("%s: IPv4 %s: %s\n", __func__, ifa->ifa_name, addr);
            } else if (ifa->ifa_addr->sa_family == AF_INET6) {
                struct sockaddr_in6* s6 = (struct sockaddr_in6*)(ifa->ifa_addr);
                CNetAddr addr(s6->sin6_addr);
                if (AddLocal(addr, LOCAL_IF))
                    LogPrintf("%s: IPv6 %s: %s\n", __func__, ifa->ifa_name, addr);
            }
        }
        freeifaddrs(myaddrs);
    }
#endif
}

void StartNode(boost::thread_group& threadGroup)
{
    uiInterface.InitMessage(translate("Loading addresses..."));
    // Load addresses for peers.dat
    int64_t nStart = GetTimeMillis();
    {
        CAddrDB adb;
        if (!adb.Read(addrman))
            LogPrintf("Invalid or missing peers.dat; recreating\n");
    }
    LogPrintf("Loaded %i addresses from peers.dat  %dms\n",
        addrman.size(), GetTimeMillis() - nStart);
    fAddressesInitialized = true;

    if (semOutbound == NULL) {
        // initialize semaphore
        int nMaxOutbound = min(MAX_OUTBOUND_CONNECTIONS, nMaxConnections);
        semOutbound = new CSemaphore(nMaxOutbound);
    }

    if (pnodeLocalHost == NULL)
        pnodeLocalHost = new CNode(&GetNodeSignals(),INVALID_SOCKET, CAddress(CService("127.0.0.1", 0), GetLocalServices()));

    Discover(threadGroup);

    //
    // Start threads
    //

    if (!settings.GetBoolArg("-dnsseed", true))
        LogPrintf("DNS seeding disabled\n");
    else
        threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "dnsseed", &ThreadDNSAddressSeed));

    // Map ports with UPnP
    MapPort(settings.GetBoolArg("-upnp", DEFAULT_UPNP));

    // Send and receive from sockets, accept connections
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "net", &ThreadSocketHandler));

    // Initiate outbound connections from -addnode
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "addcon", &ThreadOpenAddedConnections));

    // Initiate outbound connections
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "opencon", &ThreadOpenConnections));

    // Process messages
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "msghand", &ThreadMessageHandler));

    // Dump network addresses
    threadGroup.create_thread(boost::bind(&LoopForever<void (*)()>, "dumpaddr", &DumpAddresses, DUMP_ADDRESSES_INTERVAL * 1000));

    // ppcoin:mint proof-of-stake blocks in the background - except on regtest where we want granular control
    const bool underRegressionTesting = Params().NetworkID() == CBaseChainParams::REGTEST;
    if (!underRegressionTesting && pwalletMain && settings.GetBoolArg("-staking", true))
        threadGroup.create_thread(boost::bind(&TraceThread<void (*)(CWallet*), CWallet*>, "stakemint", &ThreadStakeMinter, pwalletMain));

    if(pwalletMain && pwalletMain->fFileBacked)
    {
        int64_t millisecondDelay = NUMBER_OF_SECONDS_IN_A_DAY * 1000;
        threadGroup.create_thread(
            (!underRegressionTesting)?
            boost::bind(&LoopForever<void (*)(const CWallet*), const CWallet*>, "monthly_backup", &ThreadBackupWallet, pwalletMain, millisecondDelay):
            boost::bind(&MockLoopForever<void (*)(const CWallet*), const CWallet*>, "monthly_backup", &ThreadBackupWallet, pwalletMain, millisecondDelay)
            );
    }
    else
        LogPrintf("Error: Wallet monthly backups not enabled. Wallet isn't backed by file\n");
}

bool StopNode()
{
    LogPrintf("StopNode()\n");
    MapPort(false);
    if (semOutbound)
        for (int i = 0; i < MAX_OUTBOUND_CONNECTIONS; i++)
            semOutbound->post();

    if (fAddressesInitialized) {
        DumpAddresses();
        fAddressesInitialized = false;
    }

    return true;
}

class CNetCleanup
{
public:
    CNetCleanup() {}

    ~CNetCleanup()
    {
        // Close sockets
        BOOST_FOREACH (CNode* pnode, vNodes)
            if (pnode->hSocket != INVALID_SOCKET)
                CloseSocket(pnode->hSocket);
        BOOST_FOREACH (ListenSocket& hListenSocket, vhListenSocket)
            if (hListenSocket.socket != INVALID_SOCKET)
                if (!CloseSocket(hListenSocket.socket))
                    LogPrintf("CloseSocket(hListenSocket) failed with error %s\n", NetworkErrorString(WSAGetLastError()));

        // clean up some globals (to help leak detection)
        BOOST_FOREACH (CNode* pnode, vNodes)
            delete pnode;
        BOOST_FOREACH (CNode* pnode, vNodesDisconnected)
            delete pnode;
        vNodes.clear();
        vNodesDisconnected.clear();
        vhListenSocket.clear();
        delete semOutbound;
        semOutbound = NULL;
        delete pnodeLocalHost;
        pnodeLocalHost = NULL;

#ifdef WIN32
        // Shutdown Windows Sockets
        WSACleanup();
#endif
    }
} instance_of_cnetcleanup;

void RelayTransaction(const CTransaction& tx)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss.reserve(10000);
    ss << tx;
    RelayTransaction(tx, ss);
}

void RelayTransaction(const CTransaction& tx, const CDataStream& ss)
{
    CInv inv(MSG_TX, tx.GetHash());
    {
        LOCK(cs_mapRelay);
        // Expire old relay messages
        while (!vRelayExpiration.empty() && vRelayExpiration.front().first < GetTime()) {
            mapRelay.erase(vRelayExpiration.front().second);
            vRelayExpiration.pop_front();
        }

        // Save original serialized message so newer versions are preserved
        mapRelay.insert(std::make_pair(inv, ss));
        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
    }
    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (!pnode->fRelayTxes)
            continue;
        LOCK(pnode->cs_filter);
        if (pnode->pfilter) {
            if (pnode->pfilter->IsRelevantAndUpdate(tx))
                pnode->PushInventory(inv);
        } else
            pnode->PushInventory(inv);
    }
}

void RelayTransactionLockReq(const CTransaction& tx, bool relayToAll)
{
    CInv inv(MSG_TXLOCK_REQUEST, tx.GetHash());

    //broadcast the new lock
    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (!relayToAll && !pnode->fRelayTxes)
            continue;

        pnode->PushMessage("ix", tx);
    }
}

void RelayInv(CInv& inv)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes){
        //if((pnode->nServices==NODE_BLOOM_WITHOUT_MN) && inv.IsMasterNodeType())continue;
        if (pnode->nVersion >= ActiveProtocol())
            pnode->PushInventory(inv);
    }
}

//
// CAddrDB
//


/** Used to pass flags to the Bind() function */
enum BindFlags {
    BF_NONE = 0,
    BF_EXPLICIT = (1U << 0),
    BF_REPORT_ERROR = (1U << 1),
    BF_WHITELIST = (1U << 2),
};
static bool Bind(UIMessenger& uiMessenger,const CService& addr, unsigned int flags)
{
    if (!(flags & BF_EXPLICIT) && IsLimited(addr))
        return false;
    std::string strError;
    if (!BindListenPort(addr, strError, (flags & BF_WHITELIST) != 0)) {
        if (flags & BF_REPORT_ERROR)
            return uiMessenger.InitError(strError);
        return false;
    }
    return true;
}

static bool fAlerts = DEFAULT_ALERTS;
bool AlertsAreEnabled()
{
    return fAlerts;
}
void EnableAlertsAccordingToSettings(const Settings& settings)
{
    fAlerts = settings.GetBoolArg("-alerts", DEFAULT_ALERTS);
}

bool SetNumberOfFileDescriptors(UIMessenger& uiMessenger, int& nFD)
{
    nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS);
    if (nFD < MIN_CORE_FILEDESCRIPTORS)
        return uiMessenger.InitError(translate("Not enough file descriptors available."));
    if (nFD - MIN_CORE_FILEDESCRIPTORS < nMaxConnections)
        nMaxConnections = nFD - MIN_CORE_FILEDESCRIPTORS;

    return true;
}

void SetNetworkingParameters()
{
    PeerBanningService::SetDefaultBanDuration(settings.GetArg("-bantime", 60 * 60 * 24));
    if (settings.ParameterIsSet("-bind") || settings.ParameterIsSet("-whitebind")) {
        // when specifying an explicit binding address, you want to listen on it
        // even when -connect or -proxy is specified
        if (settings.SoftSetBoolArg("-listen", true))
            LogPrintf("InitializeDivi : parameter interaction: -bind or -whitebind set -> setting -listen=1\n");
    }

    if (settings.ParameterIsSet("-connect") && settings.GetMultiParameter("-connect").size() > 0) {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        if (settings.SoftSetBoolArg("-dnsseed", false))
            LogPrintf("InitializeDivi : parameter interaction: -connect set -> setting -dnsseed=0\n");
        if (settings.SoftSetBoolArg("-listen", false))
            LogPrintf("InitializeDivi : parameter interaction: -connect set -> setting -listen=0\n");
    }

    if (settings.ParameterIsSet("-proxy")) {
        // to protect privacy, do not listen by default if a default proxy server is specified
        if (settings.SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -listen=0\n", __func__);
        // to protect privacy, do not use UPNP when a proxy is set. The user may still specify -listen=1
        // to listen locally, so don't rely on this happening through -listen below.
        if (settings.SoftSetBoolArg("-upnp", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -upnp=0\n", __func__);
        // to protect privacy, do not discover addresses by default
        if (settings.SoftSetBoolArg("-discover", false))
            LogPrintf("InitializeDivi : parameter interaction: -proxy set -> setting -discover=0\n");
    }

    if (!settings.GetBoolArg("-listen", true)) {
        // do not map ports or try to retrieve public IP when not listening (pointless)
        if (settings.SoftSetBoolArg("-upnp", false))
            LogPrintf("InitializeDivi : parameter interaction: -listen=0 -> setting -upnp=0\n");
        if (settings.SoftSetBoolArg("-discover", false))
            LogPrintf("InitializeDivi : parameter interaction: -listen=0 -> setting -discover=0\n");
        if (settings.SoftSetBoolArg("-listenonion", false))
            LogPrintf("InitializeDivi : parameter interaction: -listen=0 -> setting -listenonion=0\n");
    }

    if (settings.ParameterIsSet("-externalip")) {
        // if an explicit public IP is specified, do not try to find others
        if (settings.SoftSetBoolArg("-discover", false))
            LogPrintf("InitializeDivi : parameter interaction: -externalip set -> setting -discover=0\n");
    }

    nConnectTimeout = settings.GetArg("-timeout", DEFAULT_CONNECT_TIMEOUT);
    if (nConnectTimeout <= 0)
        nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;

    EnableAlertsAccordingToSettings(settings);
    if (settings.GetBoolArg("-peerbloomfilters", DEFAULT_PEERBLOOMFILTERS))
        EnableBloomFilters();

    const int reservedFileDescriptors = MIN_CORE_FILEDESCRIPTORS;
    int nBind = std::max((int)settings.ParameterIsSet("-bind") + (int)settings.ParameterIsSet("-whitebind"), 1);
    nMaxConnections = settings.GetArg("-maxconnections", 125);
    nMaxConnections = std::max(std::min(nMaxConnections, (int)(FD_SETSIZE - nBind - reservedFileDescriptors)), 0);
}

bool InitializeP2PNetwork(UIMessenger& uiMessenger)
{
    RegisterNodeSignals(GetNodeSignals());
    if (settings.ParameterIsSet("-onlynet")) {
        std::set<enum Network> nets;
        BOOST_FOREACH (std::string snet, settings.GetMultiParameter("-onlynet")) {
            enum Network net = ParseNetwork(snet);
            if (net == NET_UNROUTABLE)
                return uiMessenger.InitError(strprintf(translate("Unknown network specified in -onlynet: '%s'"), snet));
            nets.insert(net);
        }
        for (int n = 0; n < NET_MAX; n++) {
            enum Network net = (enum Network)n;
            if (!nets.count(net))
                SetLimited(net);
        }
    }

    if (settings.ParameterIsSet("-whitelist")) {
        BOOST_FOREACH (const std::string& net, settings.GetMultiParameter("-whitelist")) {
            CSubNet subnet(net);
            if (!subnet.IsValid())
                return uiMessenger.InitError(strprintf(translate("Invalid netmask specified in -whitelist: '%s'"), net));
            AddWhitelistedRange(subnet);
        }
    }

    // Check for host lookup allowed before parsing any network related parameters
    fNameLookup = settings.GetBoolArg("-dns", DEFAULT_NAME_LOOKUP);

    bool proxyRandomize = settings.GetBoolArg("-proxyrandomize", true);
    // -proxy sets a proxy for all outgoing network traffic
    // -noproxy (or -proxy=0) as well as the empty string can be used to not set a proxy, this is the default
    std::string proxyArg = settings.GetArg("-proxy", "");
    if (proxyArg != "" && proxyArg != "0") {
        CService proxyAddr;
        if (!Lookup(proxyArg.c_str(), proxyAddr, 9050, fNameLookup)) {
            return uiMessenger.InitError(strprintf(translate("Invalid -proxy address or hostname: '%s'"), proxyArg));
        }

        proxyType addrProxy = proxyType(proxyAddr, proxyRandomize);
        if (!addrProxy.IsValid())
            return uiMessenger.InitError(strprintf(translate("Invalid -proxy address or hostname: '%s'"), proxyArg));

        SetProxy(NET_IPV4, addrProxy);
        SetProxy(NET_IPV6, addrProxy);
        SetProxy(NET_TOR, addrProxy);
        SetNameProxy(addrProxy);
        SetReachable(NET_TOR); // by default, -proxy sets onion as reachable, unless -noonion later
    }

    // -onion can be used to set only a proxy for .onion, or override normal proxy for .onion addresses
    // -noonion (or -onion=0) disables connecting to .onion entirely
    // An empty string is used to not override the onion proxy (in which case it defaults to -proxy set above, or none)
    std::string onionArg = settings.GetArg("-onion", "");
    if (onionArg != "") {
        if (onionArg == "0") { // Handle -noonion/-onion=0
            SetReachable(NET_TOR, false); // set onions as unreachable
        } else {
            CService onionProxy;
            if (!Lookup(onionArg.c_str(), onionProxy, 9050, fNameLookup)) {
                return uiMessenger.InitError(strprintf(translate("Invalid -onion address or hostname: '%s'"), onionArg));
            }
            proxyType addrOnion = proxyType(onionProxy, proxyRandomize);
            if (!addrOnion.IsValid())
                return uiMessenger.InitError(strprintf(translate("Invalid -onion address or hostname: '%s'"), onionArg));
            SetProxy(NET_TOR, addrOnion);
            SetReachable(NET_TOR);
        }
    }

    // see Step 2: parameter interactions for more information about these
    fListen = settings.GetBoolArg("-listen", DEFAULT_LISTEN);
    fDiscover = settings.GetBoolArg("-discover", true);

    bool fBound = false;
    if (fListen) {
        if (settings.ParameterIsSet("-bind") || settings.ParameterIsSet("-whitebind")) {
            BOOST_FOREACH (std::string strBind, settings.GetMultiParameter("-bind")) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false))
                    return uiMessenger.InitError(strprintf(translate("Cannot resolve -bind address: '%s'"), strBind));
                fBound |= Bind(uiMessenger,addrBind, (BF_EXPLICIT | BF_REPORT_ERROR));
            }
            BOOST_FOREACH (std::string strBind, settings.GetMultiParameter("-whitebind")) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, 0, false))
                    return uiMessenger.InitError(strprintf(translate("Cannot resolve -whitebind address: '%s'"), strBind));
                if (addrBind.GetPort() == 0)
                    return uiMessenger.InitError(strprintf(translate("Need to specify a port with -whitebind: '%s'"), strBind));
                fBound |= Bind(uiMessenger,addrBind, (BF_EXPLICIT | BF_REPORT_ERROR | BF_WHITELIST));
            }
        } else {
            struct in_addr inaddr_any;
            inaddr_any.s_addr = INADDR_ANY;
            fBound |= Bind(uiMessenger,CService(in6addr_any, GetListenPort()), BF_NONE);
            fBound |= Bind(uiMessenger,CService(inaddr_any, GetListenPort()), !fBound ? BF_REPORT_ERROR : BF_NONE);
        }
        if (!fBound)
            return uiMessenger.InitError(translate("Failed to listen on any port. Use -listen=0 if you want this."));
    }

    if (settings.ParameterIsSet("-externalip")) {
        BOOST_FOREACH (std::string strAddr, settings.GetMultiParameter("-externalip")) {
            CService addrLocal(strAddr, GetListenPort(), fNameLookup);
            if (!addrLocal.IsValid())
                return uiMessenger.InitError(strprintf(translate("Cannot resolve -externalip address: '%s'"), strAddr));
            AddLocal(CService(strAddr, GetListenPort(), fNameLookup), LOCAL_MANUAL);
        }
    }

    BOOST_FOREACH (std::string strDest, settings.GetMultiParameter("-seednode"))
        AddOneShot(strDest);

    return true;
}