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
#include <alert.h>
#include <bloom.h>
#include <chain.h>
#include "chainparams.h"
#include "clientversion.h"
#include "miner.h"
#include "obfuscation.h"
#include "primitives/transaction.h"
#include "ui_interface.h"
#include <timeIntervalConstants.h>
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
#include <uint256.h>
#include <utilstrencodings.h>
#include <random.h>
#include <hash.h>
#include <limitedmap.h>
#include <protocol.h>
#include <streams.h>
#include <PeerNotificationOfMintService.h>
#include <NodeStats.h>
#include <NodeStateRegistry.h>
#include <Node.h>
#include <I_CommunicationRegistrar.h>
#include <NodeState.h>
#include <SocketChannel.h>

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

class PeerSyncQueryService: public I_PeerSyncQueryService
{
private:
    const std::vector<CNode*>& peers_;
    CCriticalSection& peersLock_;
public:
    PeerSyncQueryService(
        const std::vector<CNode*>& peers,
        CCriticalSection& peersLock
        ): peers_(peers)
        , peersLock_(peersLock)
    {
    }
    virtual std::vector<NodeRef> GetSporkSyncedOrInboundNodes() const
    {
        std::vector<NodeRef> vSporkSyncedNodes;
        {
            TRY_LOCK(peersLock_, lockedNodes);
            if (!lockedNodes) return {};
            for(CNode* node: peers_)
            {
                if(node->fInbound || node->AreSporksSynced())
                {
                    vSporkSyncedNodes.push_back(NodeReferenceFactory::makeUniqueNodeReference(node));
                }
            }
        }
        return vSporkSyncedNodes;
    }
};

//
// Global state variables
//
bool fDiscover = true;
bool fListen = true;

int nMaxConnections = 125;
bool fAddressesInitialized = false;
class NodeWithSocket
{
private:
    std::unique_ptr<SocketChannel> channel_;
    std::unique_ptr<CNode> node_;
public:
    template <typename ...Args>
    NodeWithSocket(
        SOCKET socket,
        Args&&... args
        ): channel_(new SocketChannel(socket))
        , node_(CNode::CreateNode(*channel_,std::forward<Args>(args)...))
    {
    }
    ~NodeWithSocket()
    {
        node_.reset();
        channel_.reset();
    }
    CNode* node()
    {
        return node_.get();
    }
    SOCKET getSocket()
    {
        return channel_->getSocket();
    }
};

class NodeManager
{
private:
    CCriticalSection cs_vNodes;
    std::vector<CNode*> vNodes_;
    std::list<std::unique_ptr<NodeWithSocket>> disconnectedNodes_;
    std::vector<ListenSocket> listeningSockets_;
    std::map<NodeId,std::unique_ptr<NodeWithSocket>> socketChannelsByNodeId_;
    NodeManager(): cs_vNodes(), vNodes_(), disconnectedNodes_(), listeningSockets_(), socketChannelsByNodeId_()
    {
    }
    void deleteNode(CNode* pnode)
    {
        AssertLockHeld(cs_vNodes);
        NodeId id = pnode->GetId();
        socketChannelsByNodeId_.erase(id);
    }
    void queueForDisconnection(CNode* pnode)
    {
        AssertLockHeld(cs_vNodes);
        NodeId id = pnode->GetId();
        disconnectedNodes_.emplace_back(socketChannelsByNodeId_[id].release());
        socketChannelsByNodeId_.erase(id);
    }
public:
    ~NodeManager()
    {
    }
    static NodeManager& Instance()
    {
        static NodeManager nodesWithSockets;
        return nodesWithSockets;
    }
    void recordNode(NodeWithSocket* pnode)
    {
        LOCK(cs_vNodes);
        vNodes_.push_back(pnode->node());
        socketChannelsByNodeId_[pnode->node()->GetId()].reset(pnode);
        vNodes_.back()->AddRef();
    }
    CCriticalSection& nodesLock()
    {
        return cs_vNodes;
    }
    std::vector<CNode*>& nodes()
    {
        return vNodes_;
    }
    std::vector<ListenSocket>& listeningSockets()
    {
        return listeningSockets_;
    }
    void addListeningSocket(SOCKET listeningSocket, bool whiteListed)
    {
        listeningSockets_.push_back(ListenSocket(listeningSocket, whiteListed));
    }
    void disconnectUnusedNodes()
    {
        LOCK(cs_vNodes);
        // Disconnect unused nodes
        std::vector<CNode*> vNodesCopy = vNodes_;
        for(CNode* pnode: vNodesCopy)
        {
            if (pnode->IsFlaggedForDisconnection() || pnode->CanBeDisconnected())
            {
                // remove from vNodes
                vNodes_.erase(remove(vNodes_.begin(), vNodes_.end(), pnode), vNodes_.end());
                // close socket and cleanup
                pnode->CloseCommsAndDisconnect();

                if (pnode->fNetworkNode || pnode->fInbound) pnode->Release();
                queueForDisconnection(pnode);
            }
        }
    }
    void deleteDisconnectedNodes(bool forceDelete = false)
    {
        // Delete disconnected nodes
        for(auto it = disconnectedNodes_.begin(); it != disconnectedNodes_.end(); )
        {
            if(forceDelete || !(*it)->node()->IsInUse())
            {
                disconnectedNodes_.erase(it++);
            }
            else
            {
                ++it;
            }
        }
    }
    void cleanupP2PConnections()
    {
        // Close sockets
        LOCK(cs_vNodes);
        for(CNode* pnode: vNodes_)
        {
            deleteNode(pnode);
        }
        vNodes_.clear();
        for(ListenSocket& hListenSocket: listeningSockets_)
        {
            if (hListenSocket.socket != INVALID_SOCKET && !CloseSocket(hListenSocket.socket))
                LogPrintf("CloseSocket(hListenSocket) failed with error %s\n", NetworkErrorString(WSAGetLastError()));
        }
        // clean up some globals (to help leak detection)
        listeningSockets_.clear();
        deleteDisconnectedNodes(true);
        disconnectedNodes_.clear();

    #ifdef WIN32
        // Shutdown Windows Sockets
        WSACleanup();
    #endif
    }
    SOCKET getSocketByNodeId(NodeId id) const
    {
        auto it = socketChannelsByNodeId_.find(id);
        if(it != socketChannelsByNodeId_.end())
        {
            return it->second->getSocket();
        }
        else
        {
            return INVALID_SOCKET;
        }
    }
};

static CCriticalSection& cs_vNodes = NodeManager::Instance().nodesLock();
static std::vector<CNode*>& vNodes = NodeManager::Instance().nodes();

PeerSyncQueryService peerSyncQueryService(vNodes,cs_vNodes);
PeerNotificationOfMintService peerBlockNotify(vNodes,cs_vNodes);
template <typename ...Args>
CNode* CreateNode(SOCKET socket, Args&&... args)
{
    NodeWithSocket* nodeWithSocket = new NodeWithSocket(socket,std::forward<Args>(args)...);
    CNode* pnode = nodeWithSocket->node();
    if (pnode->CommunicationChannelIsValid() && !pnode->fInbound)
        pnode->PushVersion(GetHeight());

    NodeManager::Instance().recordNode(nodeWithSocket);
    return pnode;
}

map<CInv, CDataStream> mapRelay;
deque<pair<int64_t, CInv> > vRelayExpiration;
CCriticalSection cs_mapRelay;

static deque<string> vOneShots;
CCriticalSection cs_vOneShots;

std::vector<std::string> vAddedNodes;
CCriticalSection cs_vAddedNodes;

boost::condition_variable messageHandlerCondition;

static CAddrMan addrman;
CAddrMan& GetNetworkAddressManager()
{
    return addrman;
}
// Signals for message handling
static CNodeSignals globalNodeSignals;
CNodeSignals& GetNodeSignals()
{
    return globalNodeSignals;
}

const bool& IsListening()
{
    return fListen;
}
int GetMaxConnections()
{
    return nMaxConnections;
}

const I_PeerSyncQueryService& GetPeerSyncQueryService()
{
    return peerSyncQueryService;
}
const I_PeerBlockNotifyService& GetPeerBlockNotifyService()
{
    return peerBlockNotify;
}
std::vector<std::string> BanOutdatedPeers()
{
    auto getSubVersion = [](std::string rawSubversion)
    {
        std::string delimiter = ": ";
        return rawSubversion.substr(rawSubversion.find(delimiter)+2);
    };
    auto versionToIndexConverter = [](std::string subVersion)
    {
        std::replace(subVersion.begin(),subVersion.end(),'.',' ');
        std::istringstream iss(subVersion);
        std::vector<std::string> results(std::istream_iterator<std::string>{iss},
                                        std::istream_iterator<std::string>());
        int64_t versionIndex = 0;
        for(int versionNumberIndex = 0; versionNumberIndex < 4 ; ++versionNumberIndex)
        {
            versionIndex = versionIndex | (std::stoi(results[versionNumberIndex]) << (21 -7*versionNumberIndex) );
        }
        return versionIndex;
    };
    int64_t referenceVersionIndex = versionToIndexConverter(
        getSubVersion(FormatSubVersion(std::vector<std::string>()))
        );

    std::vector<std::string> bannedNodeAddresses;
    LOCK(cs_vNodes);
    for (CNode* pnode: vNodes)
    {
        std::string subVersion = getSubVersion(pnode->cleanSubVer);
        if(versionToIndexConverter(subVersion) < referenceVersionIndex)
        {
            PeerBanningService::LifetimeBan(pnode->addr);
            bannedNodeAddresses.push_back(pnode->addr.ToString() );
            pnode->FlagForDisconnection();
        }
    }
    return bannedNodeAddresses;
}
bool BanSpecificPeer(const CNetAddr& address)
{
    LOCK(cs_vNodes);
    for (CNode* pnode: vNodes)
    {
        if(strcmp(address.ToString().c_str(), pnode->addr.ToString().c_str() ) == 0)
        {
            PeerBanningService::LifetimeBan(pnode->addr);
            pnode->FlagForDisconnection();
            return true;
        }
    }
    return false;
}
int GetPeerCount()
{
    LOCK(cs_vNodes);
    return vNodes.size();
}
void SchedulePingingPeers()
{
    LOCK(cs_vNodes);
    for(CNode* pNode: vNodes)
    {
        pNode->fPingQueued = true;
    }
}
NodeConnectionStatus GetConnectionStatus(const CService& addrNode)
{
    LOCK(cs_vNodes);
    for(CNode* pnode: vNodes)
    {
        if (pnode->addr == addrNode)
        {
            return (pnode->fInbound)? NodeConnectionStatus::INBOUND: NodeConnectionStatus::OUTBOUND;
        }
    }
    return NodeConnectionStatus::NOT_CONNECTED;
}

void GetNodeStateStats(std::vector<std::pair<CNodeStats,CNodeStateStats>>& vstats)
{
    vstats.clear();

    LOCK(cs_vNodes);
    vstats.reserve(vNodes.size());
    for(CNode* pnode: vNodes)
    {
        const CNodeState* state = pnode->GetNodeState();
        CNodeStateStats stateStats;
        stateStats.stateFound = static_cast<bool>(state);
        if(stateStats.stateFound)
        {
            stateStats.nMisbehavior = state->GetMisbehaviourPenalty();
            stateStats.nSyncHeight = state->pindexBestKnownBlock ? state->pindexBestKnownBlock->nHeight : -1;
            stateStats.nCommonHeight = state->pindexLastCommonBlock ? state->pindexLastCommonBlock->nHeight : -1;
            stateStats.vHeightInFlight = GetBlockHeightsInFlight(state->nodeId);
        }
        vstats.push_back(std::make_pair(CNodeStats(pnode),stateStats));
    }

}

void AddOneShot(string strDest)
{
    LOCK(cs_vOneShots);
    vOneShots.push_back(strDest);
}

// Is our peer's addrLocal potentially useful as an external IP source?
bool PeersLocalAddressIsGood(CNode* pnode)
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
        if (PeersLocalAddressIsGood(pnode) && (!addrLocal.IsRoutable() ||
                                              GetRand((GetnScore(addrLocal) > LOCAL_MANUAL) ? 8 : 2) == 0)) {
            addrLocal.SetIP(pnode->addrLocal);
        }
        if (addrLocal.IsRoutable()) {
            LogPrintf("AdvertizeLocal: advertizing address %s\n", addrLocal);
            pnode->PushAddress(addrLocal);
        }
    }
}
/** Register with a network node to receive its signals */
void RegisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.InitializeNode.connect(&InitializeNode);
    nodeSignals.FinalizeNode.connect(&FinalizeNode);
    nodeSignals.ProcessReceivedMessages.connect(&ProcessReceivedMessages);
    nodeSignals.SendMessages.connect(&SendMessages);
    nodeSignals.RespondToRequestForDataFrom.connect(&RespondToRequestForDataFrom);
    nodeSignals.AdvertizeLocalAddress.connect(&AdvertizeLocal);
}
/** Unregister a network node */
void UnregisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.InitializeNode.disconnect(&InitializeNode);
    nodeSignals.FinalizeNode.disconnect(&FinalizeNode);
    nodeSignals.ProcessReceivedMessages.disconnect(&ProcessReceivedMessages);
    nodeSignals.SendMessages.disconnect(&SendMessages);
    nodeSignals.RespondToRequestForDataFrom.connect(&RespondToRequestForDataFrom);
    nodeSignals.AdvertizeLocalAddress.connect(&AdvertizeLocal);
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

NodeRef ConnectNode(CAddress addrConnect, const char* pszDest = NULL, const bool weOpenedNetworkConnection = false)
{
    if (pszDest == NULL) {
        // we clean masternode connections in CMasternodeMan::ProcessMasternodeConnections()
        // so should be safe to skip this and connect to local Hot MN on CActiveMasternode::ManageStatus()
        if (IsLocal(addrConnect))
            return NodeReferenceFactory::makeUniqueNodeReference(nullptr);

        // Look for an existing connection
        CNode* pnode = FindNode((CService)addrConnect);
        if (pnode) {
            return NodeReferenceFactory::makeUniqueNodeReference(pnode);
        }
    }

    /// debug print
    LogPrint("net", "trying connection %s lastseen=%.1fhrs\n",
        pszDest ? pszDest : addrConnect.ToString(),
        pszDest ? 0.0 : (double)(GetAdjustedTime() - addrConnect.nTime) / 3600.0);

    // Connect
    CAddrMan& addrman = GetNetworkAddressManager();
    SOCKET hSocket;
    bool proxyConnectionFailed = false;
    if (pszDest ? ConnectSocketByName(addrConnect, hSocket, pszDest, Params().GetDefaultPort(), nConnectTimeout, &proxyConnectionFailed) :
                  ConnectSocket(addrConnect, hSocket, nConnectTimeout, &proxyConnectionFailed)) {
        if (!IsSelectableSocket(hSocket)) {
            LogPrintf("Cannot create connection: non-selectable socket created (fd >= FD_SETSIZE ?)\n");
            CloseSocket(hSocket);
            return NodeReferenceFactory::makeUniqueNodeReference(nullptr);
        }

        addrman.Attempt(addrConnect);

        // Add node
        CNode* pnode = CreateNode(hSocket,&GetNodeSignals(),GetNetworkAddressManager(), addrConnect, pszDest ? pszDest : "", false,false);
        pnode->nTimeConnected = GetTime();
        if(weOpenedNetworkConnection) pnode->fNetworkNode = true;
        return NodeReferenceFactory::makeUniqueNodeReference(pnode);
    } else if (!proxyConnectionFailed) {
        // If connecting to the node failed, and failure is not caused by a problem connecting to
        // the proxy, mark this as an attempt.
        addrman.Attempt(addrConnect);
    }

    return NodeReferenceFactory::makeUniqueNodeReference(nullptr);
}
bool CheckNodeIsAcceptingConnections(CAddress addrToConnectTo)
{
    NodeRef pnode = ConnectNode(addrToConnectTo, NULL);
    return static_cast<bool>(pnode);
}

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

class ThreadSafeNodesCopy
{
private:
    CCriticalSection& nodesLock_;
    std::vector<NodeRef> copyOfNodes;
    bool clearedCopy;
public:
    ThreadSafeNodesCopy(
        CCriticalSection& nodesLock,
        const std::vector<CNode*>& originalNodes
        ): nodesLock_(nodesLock)
        , copyOfNodes()
        , clearedCopy(true)
    {
        LOCK(nodesLock_);
        for(CNode* pnode: originalNodes)
            copyOfNodes.push_back(NodeReferenceFactory::makeUniqueNodeReference(pnode));
        clearedCopy = false;
    }
    ~ThreadSafeNodesCopy()
    {
        if(!clearedCopy) ClearCopy();
    }
    void ClearCopy()
    {
        LOCK(nodesLock_);
        for(NodeRef& nodeRef: copyOfNodes)
        {
            nodeRef.reset();
        }
        copyOfNodes.clear();
        clearedCopy = true;
    }
    const std::vector<NodeRef>& Nodes()
    {
        return copyOfNodes;
    }
};

class SocketsProcessor final: public I_CommunicationRegistrar<SOCKET>
{
private:
    struct timeval timeout;
    fd_set fdsetRecv;
    fd_set fdsetSend;
    fd_set fdsetError;
    SOCKET hSocketMax;
    bool have_fds;
    std::vector<ListenSocket>& listeningSockets_;
public:
    SocketsProcessor(
        std::vector<ListenSocket>& listeningSockets
        ): hSocketMax(0)
        , have_fds(false)
        , listeningSockets_(listeningSockets)
    {
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000; // frequency to poll pnode->vSend
        FD_ZERO(&fdsetRecv);
        FD_ZERO(&fdsetSend);
        FD_ZERO(&fdsetError);
    }

    virtual void RegisterForErrors(SOCKET hSocket)
    {
        FD_SET(hSocket, &fdsetError);
        hSocketMax = std::max(hSocketMax, hSocket);
    }
    virtual void RegisterForSend(SOCKET hSocket)
    {
        FD_SET(hSocket, &fdsetSend);
    }
    virtual void RegisterForReceive(SOCKET hSocket)
    {
        FD_SET(hSocket, &fdsetRecv);
    }
    virtual bool IsRegisteredForErrors(SOCKET hSocket) const
    {
        return FD_ISSET(hSocket, &fdsetError);
    }
    virtual bool IsRegisteredForSend(SOCKET hSocket) const
    {
        return FD_ISSET(hSocket, &fdsetSend);
    }
    virtual bool IsRegisteredForReceive(SOCKET hSocket) const
    {
        return FD_ISSET(hSocket, &fdsetRecv);
    }

    void ProcessListeningSockets()
    {
        for (const ListenSocket& hListenSocket: listeningSockets_)
        {
            FD_SET(hListenSocket.socket, &fdsetRecv);
            hSocketMax = max(hSocketMax, hListenSocket.socket);
            have_fds = true;
        }
    }
    // Implement the following logic in AssignNodesToSendOrReceiveTasks:
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
    void AssignNodesToSendOrReceiveTasks(CCriticalSection& nodesLock, std::vector<CNode*>& nodes)
    {
        LOCK(nodesLock);
        for(CNode* pnode: nodes)
        {
            if (!pnode->CommunicationChannelIsValid())
                continue;
            have_fds = true;
            SOCKET nodeSocket = NodeManager::Instance().getSocketByNodeId(pnode->GetId());
            RegisterForErrors(nodeSocket);
            CommsMode mode = pnode->SelectCommunicationMode();
            switch (mode)
            {
            case SEND:
                RegisterForSend(nodeSocket);
                break;
            case RECEIVE:
                RegisterForReceive(nodeSocket);
                break;
            case BUSY:
                break;
            }
        }
    }
    int CheckSocketCanBeSelected()
    {
        return select(have_fds ? hSocketMax + 1 : 0,
            &fdsetRecv, &fdsetSend, &fdsetError, &timeout);
    }
    void ProcessSocketErrorCode(int socketErrorCode)
    {
        if (socketErrorCode == SOCKET_ERROR)
        {
            if (have_fds)
            {
                int nErr = WSAGetLastError();
                LogPrintf("socket select error %s\n", NetworkErrorString(nErr));
                for (unsigned int i = 0; i <= hSocketMax; i++)
                    FD_SET(i, &fdsetRecv);
            }
            FD_ZERO(&fdsetSend);
            FD_ZERO(&fdsetError);
            MilliSleep(timeout.tv_usec / 1000);
        }
    }
    void AcceptNewConnections(CCriticalSection& nodesLock, std::vector<CNode*>& nodes)
    {
        for(const ListenSocket& hListenSocket: listeningSockets_)
        {
            if (hListenSocket.socket != INVALID_SOCKET && FD_ISSET(hListenSocket.socket, &fdsetRecv))
            {
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
                    LOCK(nodesLock);
                    BOOST_FOREACH (CNode* pnode, nodes)
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
                    CreateNode(hSocket,&GetNodeSignals(),GetNetworkAddressManager(), addr, "", true, whitelisted);
                }
            }
        }
    }

    bool SocketReceiveDataFromPeer(CNode* pnode, boost::condition_variable& messageHandlerCondition)
    {
        if (!pnode->CommunicationChannelIsValid())
            return false;
        SOCKET nodeSocket = NodeManager::Instance().getSocketByNodeId(pnode->GetId());
        if (IsRegisteredForReceive(nodeSocket) || IsRegisteredForErrors(nodeSocket))
            return pnode->TryReceiveData(messageHandlerCondition);
        return true;
    }
    bool SocketSendDataToPeer(CNode* pnode)
    {
        if (!pnode->CommunicationChannelIsValid())
            return false;
        SOCKET nodeSocket = NodeManager::Instance().getSocketByNodeId(pnode->GetId());
        if (IsRegisteredForSend(nodeSocket))
            return pnode->TrySendData();

        return true;
    }
};

void ThreadSocketHandler()
{
    unsigned int nPrevNodeCount = 0;
    while (true) {
        //
        // Disconnect nodes
        //
        NodeManager::Instance().disconnectUnusedNodes();
        NodeManager::Instance().deleteDisconnectedNodes();
        size_t vNodesSize = GetPeerCount();
        if(vNodesSize != nPrevNodeCount) {
            nPrevNodeCount = vNodesSize;
            uiInterface.NotifyNumConnectionsChanged(nPrevNodeCount);
        }

        SocketsProcessor socketsProcessor(NodeManager::Instance().listeningSockets());
        socketsProcessor.ProcessListeningSockets();
        socketsProcessor.AssignNodesToSendOrReceiveTasks(cs_vNodes,vNodes);

        int nSelect = socketsProcessor.CheckSocketCanBeSelected();
        boost::this_thread::interruption_point();
        socketsProcessor.ProcessSocketErrorCode(nSelect);
        socketsProcessor.AcceptNewConnections(cs_vNodes,vNodes);

        //
        // Service each socket
        //
        ThreadSafeNodesCopy safeNodesCopy(cs_vNodes,vNodes);
        for(const NodeRef& pnode: safeNodesCopy.Nodes())
        {
            boost::this_thread::interruption_point();

            if(!socketsProcessor.SocketReceiveDataFromPeer(pnode.get(),messageHandlerCondition) || !socketsProcessor.SocketSendDataToPeer(pnode.get()))
                continue;

            //
            // Inactivity checking
            //
            pnode->CheckForInnactivity();
        }
        safeNodesCopy.ClearCopy();
    }
}

void ThreadMessageHandler()
{
    boost::mutex condition_mutex;
    boost::unique_lock<boost::mutex> lock(condition_mutex);

    /* We periodically rebroadcast our address.  This is the last time
       we did a broadcast.  */
    int64_t nLastRebroadcast = 0;

    SetThreadPriority(THREAD_PRIORITY_BELOW_NORMAL);
    while (true) {
        ThreadSafeNodesCopy safeNodesCopy(cs_vNodes,vNodes);
        const std::vector<NodeRef>& vNodesCopy = safeNodesCopy.Nodes();

        bool rebroadcast = (!IsInitialBlockDownload() && (GetTime() > nLastRebroadcast + 24 * 60 * 60));

        // Poll the connected nodes for messages
        CNode* pnodeTrickle = vNodesCopy.empty()? nullptr: vNodesCopy[GetRand(vNodesCopy.size())].get();
        bool fSleep = true;

        for(const NodeRef& pnode: vNodesCopy)
        {
            if (pnode->IsFlaggedForDisconnection())
                continue;

            // Receive messages
            pnode->ProcessReceiveMessages(fSleep);
            boost::this_thread::interruption_point();

            // Handle potential ping messages first.
            if (pnode->CanSendMessagesToPeer())
            {
                pnode->MaybeSendPing();
            }
            boost::this_thread::interruption_point();

            // Rebroadcasting is done for all nodes after the ping
            // of the first; this is to mimic previous behaviour.
            if (rebroadcast)
            {
                for (const NodeRef& nodeRef : vNodesCopy)
                {
                    NodeRef nodeToAdvertiseAddressTo = NodeReferenceFactory::makeUniqueNodeReference(nodeRef.get());
                    nodeToAdvertiseAddressTo->AdvertizeLocalAddress(nLastRebroadcast);
                    boost::this_thread::interruption_point();
                }

                nLastRebroadcast = GetTime();
                rebroadcast = false;
            }

            // Send messages
            if (pnode->CanSendMessagesToPeer())
            {
                pnode->ProcessSendMessages(pnode.get() == pnodeTrickle);
            }
            boost::this_thread::interruption_point();
        }

        safeNodesCopy.ClearCopy();

        if (fSleep)
            messageHandlerCondition.timed_wait(lock, boost::posix_time::microsec_clock::universal_time() + boost::posix_time::milliseconds(100));
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
    CAddrMan& addrman = GetNetworkAddressManager();
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
    CAddrMan& addrman =GetNetworkAddressManager();
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
    if (!OpenNetworkConnection(addr,strDest.c_str(), true))
        AddOneShot(strDest);
}

void ThreadOpenConnections()
{
    CAddrMan& addrman = GetNetworkAddressManager();
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
                    OpenNetworkConnection(addr, strAddr.c_str());
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
            OpenNetworkConnection(addrConnect);
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
                OpenNetworkConnection(addr, strAddNode.c_str());
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
            OpenNetworkConnection(CAddress(vserv[i % vserv.size()]));
            MilliSleep(500);
        }
        MilliSleep(120000); // Retry every 2 minutes
    }
}

// if successful, this moves the passed grant to the constructed node
bool OpenNetworkConnection(const CAddress& addrConnect, const char* pszDest, bool fOneShot)
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

    NodeRef pnode = ConnectNode(addrConnect, pszDest,true);
    boost::this_thread::interruption_point();

    if (!pnode)
        return false;
    if (fOneShot)
        pnode->fOneShot = true;

    return true;
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

    NodeManager::Instance().addListeningSocket(hListenSocket,fWhitelisted);
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

void StartNode(boost::thread_group& threadGroup,const bool& reindexFlag, CWallet* pwalletMain)
{
    uiInterface.InitMessage(translate("Loading addresses..."));
    // Load addresses for peers.dat
    int64_t nStart = GetTimeMillis();
    {
        CAddrDB adb;
        if (!adb.Read(GetNetworkAddressManager()))
            LogPrintf("Invalid or missing peers.dat; recreating\n");
    }
    LogPrintf("Loaded %i addresses from peers.dat  %dms\n",
        GetNetworkAddressManager().size(), GetTimeMillis() - nStart);
    fAddressesInitialized = true;

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
    threadGroup.create_thread(
        boost::bind(&TraceThread<void (*)()>, "msghand", &ThreadMessageHandler) );

    // Dump network addresses
    threadGroup.create_thread(boost::bind(&LoopForever<void (*)()>, "dumpaddr", &DumpAddresses, DUMP_ADDRESSES_INTERVAL * 1000));

    // ppcoin:mint proof-of-stake blocks in the background - except on regtest where we want granular control
    InitializeCoinMintingModule(GetPeerBlockNotifyService(), static_cast<I_StakingWallet*>(pwalletMain));
    const bool underRegressionTesting = Params().NetworkID() == CBaseChainParams::REGTEST;
    if (!underRegressionTesting && pwalletMain && settings.GetBoolArg("-staking", true))
    {
        threadGroup.create_thread(
            boost::bind(
                &TraceThread<void (*)()>,
                "coinmint",
                &ThreadCoinMinter));
    }
}

bool StopNode()
{
    LogPrintf("StopNode()\n");
    MapPort(false);

    if (fAddressesInitialized) {
        DumpAddresses();
        fAddressesInitialized = false;
    }

    DestructCoinMintingModule();

    return true;
}

void CleanupP2PConnections()
 {
    NodeManager::Instance().cleanupP2PConnections();
}
void NotifyPeersOfNewChainTip(const int chainHeight, const uint256& updatedBlockHashForChainTip, const int fallbackPeerChainHeightEstimate)
{
    LOCK(cs_vNodes);
    for (CNode* pnode : vNodes) {
        if (chainHeight > (pnode->nStartingHeight != -1 ? pnode->nStartingHeight - 2000 : fallbackPeerChainHeightEstimate))
            pnode->PushInventory(CInv(MSG_BLOCK, updatedBlockHashForChainTip));
    }
}
void DeterministicallyRelayAddressToLimitedPeers(const CAddress& addr,int numberOfNodes)
{
    LOCK(cs_vNodes);
    // Use deterministic randomness to send to the same nodes for 24 hours
    // at a time so the setAddrKnowns of the chosen nodes prevent repeats
    static uint256 hashSalt;
    if (hashSalt == 0)
        hashSalt = GetRandHash();
    uint64_t hashAddr = addr.GetHash();
    uint256 hashRand = hashSalt ^ (hashAddr << 32) ^ ((GetTime() + hashAddr) / (24 * 60 * 60));
    hashRand = Hash(BEGIN(hashRand), END(hashRand));
    std::multimap<uint256, CNode*> mapMix;
    for(CNode* pnode: vNodes) {
        if (pnode->nVersion < CADDR_TIME_VERSION)
            continue;
        unsigned int nPointer;
        memcpy(&nPointer, &pnode, sizeof(nPointer));
        uint256 hashKey = hashRand ^ nPointer;
        hashKey = Hash(BEGIN(hashKey), END(hashKey));
        mapMix.insert(make_pair(hashKey, pnode));
    }
    for (std::multimap<uint256, CNode*>::iterator mi = mapMix.begin(); mi != mapMix.end() && numberOfNodes-- > 0; ++mi)
        ((*mi).second)->PushAddress(addr);
}

bool ShouldRelayAlertToPeer(const CAlert& alert, CNode* pnode)
{
    if (!alert.IsInEffect())
        return false;
    // don't relay to nodes which haven't sent their version message
    if (pnode->nVersion == 0)
        return false;
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(alert.GetHash()).second) {
        if (alert.AppliesTo(pnode->nVersion, pnode->strSubVer) ||
            alert.AppliesToMe() ||
            GetAdjustedTime() < alert.nRelayUntil) {
            return true;
        }
    }
    return false;
}

void RelayAlertToPeers(const CAlert& alert)
{
    LOCK(cs_vNodes);
    for(CNode* pnode: vNodes)
    {
        if(ShouldRelayAlertToPeer(alert,pnode))
        {
            pnode->PushMessage("alert", alert);
        }
    }
}
void RelayAllAlertsTo(CNode* peer)
{
    const std::vector<CAlert> allAlerts  = CAlert::GetAllAlerts();
    for(const CAlert& alert: allAlerts)
    {
        if(ShouldRelayAlertToPeer(alert,peer))
        {
            peer->PushMessage("alert", alert);
        }
    }
}

bool RepeatRelayedInventory(CNode* pfrom, const CInv& inv)
{
    LOCK(cs_mapRelay);
    std::map<CInv, CDataStream>::iterator mi = mapRelay.find(inv);
    if (mi != mapRelay.end()) {
        pfrom->PushMessage(inv.GetCommand(), (*mi).second);
        return true;
    }
    return false;
}

static void RelayTransactionToAllPeers(const CTransaction& tx, const CDataStream& ss)
{
    CInv inv(MSG_TX, tx.GetHash());
    {
        LOCK(cs_mapRelay);
        // Expire old relay messages
        while (!vRelayExpiration.empty() && vRelayExpiration.front().first < GetTime())
        {
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
        if (pnode->pfilter)
        {
            if (pnode->pfilter->IsRelevantAndUpdate(tx))
                pnode->PushInventory(inv);
        }
        else
        {
            pnode->PushInventory(inv);
        }
    }
}
void RelayTransactionToAllPeers(const CTransaction& tx)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss.reserve(10000);
    ss << tx;
    RelayTransactionToAllPeers(tx, ss);
}


void RelayInv(CInv& inv)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes){
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
